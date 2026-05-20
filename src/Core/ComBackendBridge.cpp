#include "ComBackendBridge.h"

#include <atlbase.h>
#include <atlcomcli.h>
#include <oleauto.h>

namespace {

/**
 * @brief Per-thread COM state used to cache initialized and connected dispatch object.
 */
struct ThreadComState
{
    ~ThreadComState()
    {
        bridge.Release();
        if (initialized) {
            CoUninitialize();
        }
    }

    std::wstring progId;
    bool initialized{false};
    bool usable{false};
    bool connected{false};
    std::wstring connectedIpAddress;
    ATL::CComPtr<IDispatch> bridge;
};

/**
 * @brief Cast enum style to legacy LONG.
 */
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

/**
 * @brief Convert HRESULT/Variant result to bridge error code.
 */
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

/**
 * @brief Resolve named member ID for IDispatch object.
 */
HRESULT ResolveDispatchId(IDispatch* dispatch, const wchar_t* name, DISPID& dispatchId)
{
    auto* mutableName = const_cast<LPOLESTR>(name);
    return dispatch->GetIDsOfNames(IID_NULL, &mutableName, 1, LOCALE_USER_DEFAULT, &dispatchId);
}

/**
 * @brief Prepare VT_I4 argument.
 */
void SetLongArgument(ATL::CComVariant& argument, LONG value)
{
    argument.Clear();
    argument.vt = VT_I4;
    argument.lVal = value;
}

/**
 * @brief Prepare VT_BSTR argument.
 */
void SetStringArgument(ATL::CComVariant& argument, const std::wstring& value)
{
    argument.Clear();
    argument.vt = VT_BSTR;
    argument.bstrVal = SysAllocString(value.c_str());
}

} // namespace

/**
 * @file ComBackendBridge.cpp
 * @brief COM bridge adapter translating IDispatch calls to internal backend operations.
 */

/**
 * @brief Construct COM bridge with target progId.
 */
ComBackendBridge::ComBackendBridge(std::wstring progId)
    : progId_(std::move(progId))
{
}

/**
 * @brief Dispose of cached thread-local COM state.
 */
ComBackendBridge::~ComBackendBridge()
{
}

BridgeError ComBackendBridge::Connect(const std::wstring& ipAddress)
{
    ATL::CComPtr<IDispatch> dispatch;
    const auto ensure = EnsureObject(dispatch, false);
    if (ensure != BridgeError::Ok) {
        return ensure;
    }

    const auto error = InvokeConnect(dispatch, ipAddress);
    if (error == BridgeError::Ok) {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        connectedIpAddress_ = ipAddress;
        hasConnectedIpAddress_ = true;
    }
    return error;
}

/**
 * @brief Invoke Connect member on COM object if present.
 */
BridgeError ComBackendBridge::InvokeConnect(IDispatch* dispatch, const std::wstring& ipAddress)
{
    DISPID dispatchId{};
    if (dispatch == nullptr || FAILED(ResolveDispatchId(dispatch, L"Connect", dispatchId))) {
        return BridgeError::InternalError;
    }

    ATL::CComVariant args[1];
    SetStringArgument(args[0], ipAddress);
    DISPPARAMS parameters{args, nullptr, 1, 0};
    ATL::CComVariant result;
    const auto hr = dispatch->Invoke(dispatchId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters, &result, nullptr, nullptr);
    return FromDispatchResult(hr, result);
}

/**
 * @brief Read value via COM and convert HRESULT into bridge status.
 */
BridgeError ComBackendBridge::Read(const DataKey& key, std::wstring& value)
{
    ATL::CComPtr<IDispatch> dispatch;
    const auto ensure = EnsureObject(dispatch, true);
    if (ensure != BridgeError::Ok) {
        value.clear();
        return ensure;
    }

    DISPID dispatchId{};
    if (FAILED(ResolveDispatchId(dispatch, L"Read", dispatchId))) {
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
    const auto hr = dispatch->Invoke(dispatchId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters, &result, nullptr, nullptr);
    const auto error = FromDispatchResult(hr, result);
    if (error != BridgeError::Ok) {
        value.clear();
        return error;
    }

    value = text.m_str == nullptr ? L"" : std::wstring(text.m_str, SysStringLen(text.m_str));
    return BridgeError::Ok;
}

/**
 * @brief Write value via COM and return status.
 */
BridgeError ComBackendBridge::Write(const DataKey& key, const std::wstring& value)
{
    ATL::CComPtr<IDispatch> dispatch;
    const auto ensure = EnsureObject(dispatch, true);
    if (ensure != BridgeError::Ok) {
        return ensure;
    }

    DISPID dispatchId{};
    if (FAILED(ResolveDispatchId(dispatch, L"Write", dispatchId))) {
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
    const auto hr = dispatch->Invoke(dispatchId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters, &result, nullptr, nullptr);
    return FromDispatchResult(hr, result);
}

/**
 * @brief Obtain COM object; initialize COM and reconnect when needed.
 */
BridgeError ComBackendBridge::EnsureObject(ATL::CComPtr<IDispatch>& dispatch, bool connectIfNeeded)
{
    thread_local ThreadComState state;
    if (state.progId != progId_) {
        state.bridge.Release();
        state.progId = progId_;
        state.connected = false;
        state.connectedIpAddress.clear();
    }

    if (!state.initialized && !state.usable) {
        const auto hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        state.initialized = hr == S_OK || hr == S_FALSE;
        state.usable = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    if (!state.usable) {
        return BridgeError::InternalError;
    }

    if (state.bridge == nullptr) {
        CLSID clsid{};
        if (FAILED(CLSIDFromProgID(progId_.c_str(), &clsid))) {
            return BridgeError::NotConnected;
        }

        ATL::CComPtr<IDispatch> created;
        const auto hr = CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_IDispatch, reinterpret_cast<void**>(&created));
        if (FAILED(hr) || created == nullptr) {
            return BridgeError::NotConnected;
        }

        state.bridge = created;
        state.connected = false;
        state.connectedIpAddress.clear();
    }

    if (connectIfNeeded) {
        std::wstring targetIpAddress;
        {
            std::lock_guard<std::mutex> lock(connectionMutex_);
            if (!hasConnectedIpAddress_) {
                return BridgeError::NotConnected;
            }
            targetIpAddress = connectedIpAddress_;
        }
        if (!state.connected || state.connectedIpAddress != targetIpAddress) {
            const auto error = InvokeConnect(state.bridge, targetIpAddress);
            if (error != BridgeError::Ok) {
                return error;
            }
            state.connected = true;
            state.connectedIpAddress = targetIpAddress;
        }
    }

    dispatch = state.bridge;
    return BridgeError::Ok;
}
