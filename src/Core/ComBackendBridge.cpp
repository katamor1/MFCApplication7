#include "ComBackendBridge.h"

#include <atlbase.h>
#include <atlcomcli.h>
#include <oleauto.h>

namespace {

LONG ToStyleValue(DataStyle style)
{
    return static_cast<LONG>(style);
}

BridgeError FromLong(LONG value)
{
    switch (static_cast<BridgeError>(value)) {
    case BridgeError::Ok:
    case BridgeError::NotConnected:
    case BridgeError::InvalidDataId:
    case BridgeError::InvalidSubDataId:
    case BridgeError::InvalidStyle:
    case BridgeError::ReadOnly:
    case BridgeError::Timeout:
    case BridgeError::InvalidIpAddress:
        return static_cast<BridgeError>(value);
    default:
        return BridgeError::InternalError;
    }
}

BridgeError FromDispatchResult(HRESULT hr, ATL::CComVariant& result)
{
    if (FAILED(hr)) {
        return BridgeError::InternalError;
    }
    if (FAILED(result.ChangeType(VT_I4))) {
        return BridgeError::InternalError;
    }
    return FromLong(result.lVal);
}

HRESULT ResolveDispatchId(IDispatch* dispatch, const wchar_t* name, DISPID& dispatchId)
{
    auto* mutableName = const_cast<LPOLESTR>(name);
    return dispatch->GetIDsOfNames(IID_NULL, &mutableName, 1, LOCALE_USER_DEFAULT, &dispatchId);
}

void SetLongArgument(ATL::CComVariant& argument, LONG value)
{
    argument.Clear();
    argument.vt = VT_I4;
    argument.lVal = value;
}

void SetStringArgument(ATL::CComVariant& argument, const std::wstring& value)
{
    argument.Clear();
    argument.vt = VT_BSTR;
    argument.bstrVal = SysAllocString(value.c_str());
}

} // namespace

ComBackendBridge::ComBackendBridge(std::wstring progId)
    : progId_(std::move(progId))
{
    const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    comInitialized_ = hr == S_OK || hr == S_FALSE;
    comUsable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

ComBackendBridge::~ComBackendBridge()
{
    bridge_.Release();
    if (comInitialized_) {
        CoUninitialize();
    }
}

BridgeError ComBackendBridge::Connect(const std::wstring& ipAddress)
{
    const auto ensure = EnsureObject();
    if (ensure != BridgeError::Ok) {
        return ensure;
    }

    DISPID dispatchId{};
    if (FAILED(ResolveDispatchId(bridge_, L"Connect", dispatchId))) {
        return BridgeError::InternalError;
    }

    ATL::CComVariant args[1];
    SetStringArgument(args[0], ipAddress);
    DISPPARAMS parameters{args, nullptr, 1, 0};
    ATL::CComVariant result;
    const auto hr = bridge_->Invoke(dispatchId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters, &result, nullptr, nullptr);
    return FromDispatchResult(hr, result);
}

BridgeError ComBackendBridge::Read(const DataKey& key, std::wstring& value)
{
    const auto ensure = EnsureObject();
    if (ensure != BridgeError::Ok) {
        value.clear();
        return ensure;
    }

    DISPID dispatchId{};
    if (FAILED(ResolveDispatchId(bridge_, L"Read", dispatchId))) {
        value.clear();
        return BridgeError::InternalError;
    }

    ATL::CComBSTR text;
    ATL::CComVariant args[5];
    args[0].vt = VT_BSTR | VT_BYREF;
    args[0].pbstrVal = &text.m_str;
    SetLongArgument(args[1], ToStyleValue(key.style));
    SetLongArgument(args[2], static_cast<LONG>(key.subId2));
    SetLongArgument(args[3], static_cast<LONG>(key.subId1));
    SetLongArgument(args[4], static_cast<LONG>(key.dataId));

    DISPPARAMS parameters{args, nullptr, 5, 0};
    ATL::CComVariant result;
    const auto hr = bridge_->Invoke(dispatchId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters, &result, nullptr, nullptr);
    const auto error = FromDispatchResult(hr, result);
    if (error != BridgeError::Ok) {
        value.clear();
        return error;
    }

    value = text.m_str == nullptr ? L"" : std::wstring(text.m_str, SysStringLen(text.m_str));
    return BridgeError::Ok;
}

BridgeError ComBackendBridge::Write(const DataKey& key, const std::wstring& value)
{
    const auto ensure = EnsureObject();
    if (ensure != BridgeError::Ok) {
        return ensure;
    }

    DISPID dispatchId{};
    if (FAILED(ResolveDispatchId(bridge_, L"Write", dispatchId))) {
        return BridgeError::InternalError;
    }

    ATL::CComVariant args[5];
    SetStringArgument(args[0], value);
    SetLongArgument(args[1], ToStyleValue(key.style));
    SetLongArgument(args[2], static_cast<LONG>(key.subId2));
    SetLongArgument(args[3], static_cast<LONG>(key.subId1));
    SetLongArgument(args[4], static_cast<LONG>(key.dataId));

    DISPPARAMS parameters{args, nullptr, 5, 0};
    ATL::CComVariant result;
    const auto hr = bridge_->Invoke(dispatchId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters, &result, nullptr, nullptr);
    return FromDispatchResult(hr, result);
}

BridgeError ComBackendBridge::EnsureObject()
{
    if (!comUsable_) {
        return BridgeError::InternalError;
    }
    if (bridge_ != nullptr) {
        return BridgeError::Ok;
    }

    CLSID clsid{};
    if (FAILED(CLSIDFromProgID(progId_.c_str(), &clsid))) {
        return BridgeError::NotConnected;
    }

    ATL::CComPtr<IDispatch> created;
    const auto hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&created));
    if (FAILED(hr) || created == nullptr) {
        return BridgeError::NotConnected;
    }

    bridge_ = created;
    return BridgeError::Ok;
}
