#include "BackendBridge.h"
#include "ComBackendBridge.h"
#include "DataCatalog.h"
#include "MockBackendBridge.h"

#include <atlbase.h>
#include <atlcom.h>
#include <oleauto.h>
#include <windows.h>

#include <cwctype>
#include <string>

class CBackendBridgeMockModule final : public ATL::CAtlExeModuleT<CBackendBridgeMockModule>
{
};

CBackendBridgeMockModule _AtlModule;

namespace {

/**
 * @file BackendBridgeMock.cpp
 * @brief ATL in-process COM server implementation for the mock backend bridge.
 */

/**
 * @brief Convert to lower-case for command line and dispatch-name matching.
 */
std::wstring ToLower(std::wstring value)
{
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

/**
 * @brief Check command line has an argument token.
 */
bool HasArgument(const std::wstring& command, const wchar_t* argument)
{
    return ToLower(command).find(ToLower(argument)) != std::wstring::npos;
}

/**
 * @brief Map numeric style value to DataStyle enum.
 */
DataStyle ToStyle(LONG style)
{
    switch (style) {
    case 0:
        return DataStyle::Raw;
    case 1:
        return DataStyle::ThousandsSeparated;
    case 2:
        return DataStyle::SecondsToHhMmSs;
    case 3:
        return DataStyle::MillimetersToInches;
    default:
        return DataStyle::Raw;
    }
}

/**
 * @brief Validate style value from COM caller.
 */
bool IsKnownStyle(LONG style)
{
    return style >= 0 && style <= 3;
}

constexpr DISPID DispatchIdConnect = 1;
constexpr DISPID DispatchIdRead = 2;
constexpr DISPID DispatchIdWrite = 3;

/**
 * @brief Read argument at zero-based logical position from reversed DISPPARAMS order.
 */
VARIANTARG* NaturalArgument(DISPPARAMS* parameters, UINT index)
{
    if (parameters == nullptr || parameters->rgvarg == nullptr || index >= parameters->cArgs) {
        return nullptr;
    }
    return &parameters->rgvarg[parameters->cArgs - index - 1];
}

/**
 * @brief Convert VARIANT to LONG with type coercion.
 */
bool VariantToLong(const VARIANTARG& argument, LONG& value)
{
    ATL::CComVariant converted;
    if (FAILED(VariantChangeType(&converted, const_cast<VARIANTARG*>(&argument), 0, VT_I4))) {
        return false;
    }
    value = converted.lVal;
    return true;
}

/**
 * @brief Convert VARIANT to wide string with fallback conversion.
 */
bool VariantToString(const VARIANTARG& argument, std::wstring& value)
{
    if (argument.vt == VT_BSTR) {
        value = argument.bstrVal == nullptr ? L"" : std::wstring(argument.bstrVal, SysStringLen(argument.bstrVal));
        return true;
    }

    ATL::CComVariant converted;
    if (FAILED(VariantChangeType(&converted, const_cast<VARIANTARG*>(&argument), 0, VT_BSTR))) {
        return false;
    }
    value = converted.bstrVal == nullptr ? L"" : std::wstring(converted.bstrVal, SysStringLen(converted.bstrVal));
    return true;
}

/**
 * @brief Write integer result into VARIANT return channel.
 */
HRESULT SetLongResult(VARIANT* result, LONG value)
{
    if (result != nullptr) {
        VariantInit(result);
        result->vt = VT_I4;
        result->lVal = value;
    }
    return S_OK;
}

/**
 * @brief Shared singleton for COM dispatch handlers.
 */
MockBackendBridge& SharedComBridge()
{
    static MockBackendBridge bridge(DataCatalog::CreateDefault());
    return bridge;
}

/**
 * @brief In-process smoke test for mock bridge methods.
 */
int RunSelfTest()
{
    auto catalog = DataCatalog::CreateDefault();
    MockBackendBridge bridge(catalog);
    if (bridge.Connect(L"127.0.0.1") != BridgeError::Ok) {
        return 10;
    }

    std::wstring value;
    if (bridge.Read({1010, 0, 0, DataStyle::ThousandsSeparated}, value) != BridgeError::Ok) {
        return 20;
    }
    if (value.find(L",") == std::wstring::npos) {
        return 21;
    }
    if (bridge.Write({2001, 1, 0, DataStyle::Raw}, L"CNT-UPDATED") != BridgeError::Ok) {
        return 30;
    }
    return 0;
}

/**
 * @brief Resolve module path used for LocalServer registration.
 */
std::wstring ModulePath()
{
    std::wstring path(MAX_PATH, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

/**
 * @brief Write REG_SZ value under given registry key.
 */
LONG SetStringValue(HKEY root, const std::wstring& subKey, const wchar_t* valueName, const std::wstring& value)
{
    HKEY key{};
    const auto createResult = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (createResult != ERROR_SUCCESS) {
        return createResult;
    }
    const auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const auto setResult = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(key);
    return setResult;
}

/**
 * @brief Register COM class/progID under HKCU.
 */
int RegisterLocalServer()
{
    const std::wstring clsid = L"{A737D261-91BC-4646-B58E-9E8B53378D6F}";
    const std::wstring progId = L"MFCApplication7.BackendBridgeMock";
    const std::wstring clsidKey = L"Software\\Classes\\CLSID\\" + clsid;
    const std::wstring quotedPath = L"\"" + ModulePath() + L"\"";

    if (SetStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + progId, nullptr, L"MFCApplication7 Backend Bridge Mock") != ERROR_SUCCESS) {
        return 1;
    }
    if (SetStringValue(HKEY_CURRENT_USER, L"Software\\Classes\\" + progId + L"\\CLSID", nullptr, clsid) != ERROR_SUCCESS) {
        return 2;
    }
    if (SetStringValue(HKEY_CURRENT_USER, clsidKey, nullptr, L"MFCApplication7 Backend Bridge Mock") != ERROR_SUCCESS) {
        return 3;
    }
    if (SetStringValue(HKEY_CURRENT_USER, clsidKey + L"\\ProgID", nullptr, progId) != ERROR_SUCCESS) {
        return 4;
    }
    if (SetStringValue(HKEY_CURRENT_USER, clsidKey + L"\\LocalServer32", nullptr, quotedPath) != ERROR_SUCCESS) {
        return 5;
    }
    return 0;
}

/**
 * @brief Unregister COM class/progID under HKCU.
 */
int UnregisterLocalServer()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\MFCApplication7.BackendBridgeMock");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\{A737D261-91BC-4646-B58E-9E8B53378D6F}");
    return 0;
}

/**
 * @brief Register COM server and run COM invocation smoke checks.
 */
int RunComSelfTest()
{
    const auto registerResult = RegisterLocalServer();
    if (registerResult != 0) {
        return 100 + registerResult;
    }

    int exitCode = 0;
    {
        ComBackendBridge bridge(L"MFCApplication7.BackendBridgeMock");
        const auto connectError = bridge.Connect(L"127.0.0.1");
        if (connectError != BridgeError::Ok) {
            exitCode = 210 + static_cast<int>(connectError);
        } else {
            std::wstring value;
            const auto readError = bridge.Read({1010, 0, 0, DataStyle::ThousandsSeparated}, value);
            if (readError != BridgeError::Ok) {
                exitCode = 220 + static_cast<int>(readError);
            } else if (value.find(L",") == std::wstring::npos) {
                exitCode = 221;
            } else {
                const auto writeError = bridge.Write({2001, 1, 0, DataStyle::Raw}, L"CNT-COM");
                if (writeError != BridgeError::Ok) {
                    exitCode = 230 + static_cast<int>(writeError);
                }
            }
        }
    }

    UnregisterLocalServer();
    return exitCode;
}

} // namespace

class ATL_NO_VTABLE CBackendBridge
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CBackendBridge, &__uuidof(BackendBridgeComClass)>
    , public IBackendBridgeCom
{
public:
    /**
     * @brief Construct COM bridge object.
     */
    CBackendBridge()
    {
    }

    DECLARE_NO_REGISTRY()

    BEGIN_COM_MAP(CBackendBridge)
        COM_INTERFACE_ENTRY(IBackendBridgeCom)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    /**
     * @brief COM call entrypoint for Connect.
     */
    HRESULT STDMETHODCALLTYPE Connect(BSTR ipAddress, LONG* errorCode) override
    {
        if (errorCode == nullptr) {
            return E_POINTER;
        }
        *errorCode = static_cast<LONG>(SharedComBridge().Connect(ipAddress == nullptr ? L"" : std::wstring(ipAddress)));
        return S_OK;
    }

    /**
     * @brief COM call entrypoint for Read.
     */
    HRESULT STDMETHODCALLTYPE Read(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR* value, LONG* errorCode) override
    {
        if (value == nullptr || errorCode == nullptr) {
            return E_POINTER;
        }
        *value = SysAllocString(L"");
        if (!IsKnownStyle(style)) {
            *errorCode = static_cast<LONG>(BridgeError::InvalidStyle);
            return S_OK;
        }

        std::wstring text;
        const auto error = SharedComBridge().Read({static_cast<int>(dataId), static_cast<int>(subId1), static_cast<int>(subId2), ToStyle(style)}, text);
        SysFreeString(*value);
        *value = SysAllocString(text.c_str());
        *errorCode = static_cast<LONG>(error);
        return S_OK;
    }

    /**
     * @brief COM call entrypoint for Write.
     */
    HRESULT STDMETHODCALLTYPE Write(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR value, LONG* errorCode) override
    {
        if (errorCode == nullptr) {
            return E_POINTER;
        }
        if (!IsKnownStyle(style)) {
            *errorCode = static_cast<LONG>(BridgeError::InvalidStyle);
            return S_OK;
        }

        *errorCode = static_cast<LONG>(SharedComBridge().Write({static_cast<int>(dataId), static_cast<int>(subId1), static_cast<int>(subId2), ToStyle(style)},
                                                               value == nullptr ? L"" : std::wstring(value)));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override
    {
        if (count == nullptr) {
            return E_POINTER;
        }
        *count = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR* names, UINT count, LCID, DISPID* dispatchIds) override
    {
        if (names == nullptr || dispatchIds == nullptr) {
            return E_POINTER;
        }

        for (UINT index = 0; index < count; ++index) {
            const std::wstring name = names[index] == nullptr ? L"" : ToLower(names[index]);
            if (name == L"connect") {
                dispatchIds[index] = DispatchIdConnect;
            } else if (name == L"read") {
                dispatchIds[index] = DispatchIdRead;
            } else if (name == L"write") {
                dispatchIds[index] = DispatchIdWrite;
            } else {
                dispatchIds[index] = DISPID_UNKNOWN;
                return DISP_E_UNKNOWNNAME;
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke(DISPID dispatchId, REFIID interfaceId, LCID, WORD flags, DISPPARAMS* parameters, VARIANT* result, EXCEPINFO*, UINT*) override
    {
        if (interfaceId != IID_NULL) {
            return DISP_E_UNKNOWNINTERFACE;
        }
        if ((flags & DISPATCH_METHOD) == 0) {
            return DISP_E_MEMBERNOTFOUND;
        }

        switch (dispatchId) {
        case DispatchIdConnect: {
            if (parameters == nullptr || parameters->cArgs != 1) {
                return DISP_E_BADPARAMCOUNT;
            }
            std::wstring ipAddress;
            auto* ipAddressArgument = NaturalArgument(parameters, 0);
            if (ipAddressArgument == nullptr || !VariantToString(*ipAddressArgument, ipAddress)) {
                return DISP_E_TYPEMISMATCH;
            }
            return SetLongResult(result, static_cast<LONG>(SharedComBridge().Connect(ipAddress)));
        }
        case DispatchIdRead: {
            if (parameters == nullptr || parameters->cArgs != 5) {
                return DISP_E_BADPARAMCOUNT;
            }

            LONG dataId{};
            LONG subId1{};
            LONG subId2{};
            LONG style{};
            auto* dataIdArgument = NaturalArgument(parameters, 0);
            auto* subId1Argument = NaturalArgument(parameters, 1);
            auto* subId2Argument = NaturalArgument(parameters, 2);
            auto* styleArgument = NaturalArgument(parameters, 3);
            auto* valueArgument = NaturalArgument(parameters, 4);
            if (dataIdArgument == nullptr ||
                subId1Argument == nullptr ||
                subId2Argument == nullptr ||
                styleArgument == nullptr ||
                valueArgument == nullptr ||
                valueArgument->vt != (VT_BSTR | VT_BYREF) ||
                valueArgument->pbstrVal == nullptr ||
                !VariantToLong(*dataIdArgument, dataId) ||
                !VariantToLong(*subId1Argument, subId1) ||
                !VariantToLong(*subId2Argument, subId2) ||
                !VariantToLong(*styleArgument, style)) {
                return DISP_E_TYPEMISMATCH;
            }

            SysFreeString(*valueArgument->pbstrVal);
            *valueArgument->pbstrVal = SysAllocString(L"");
            if (!IsKnownStyle(style)) {
                return SetLongResult(result, static_cast<LONG>(BridgeError::InvalidStyle));
            }

            std::wstring text;
            const auto error = SharedComBridge().Read({static_cast<int>(dataId), static_cast<int>(subId1), static_cast<int>(subId2), ToStyle(style)}, text);
            SysFreeString(*valueArgument->pbstrVal);
            *valueArgument->pbstrVal = SysAllocString(text.c_str());
            return SetLongResult(result, static_cast<LONG>(error));
        }
        case DispatchIdWrite: {
            if (parameters == nullptr || parameters->cArgs != 5) {
                return DISP_E_BADPARAMCOUNT;
            }

            LONG dataId{};
            LONG subId1{};
            LONG subId2{};
            LONG style{};
            std::wstring value;
            auto* dataIdArgument = NaturalArgument(parameters, 0);
            auto* subId1Argument = NaturalArgument(parameters, 1);
            auto* subId2Argument = NaturalArgument(parameters, 2);
            auto* styleArgument = NaturalArgument(parameters, 3);
            auto* valueArgument = NaturalArgument(parameters, 4);
            if (dataIdArgument == nullptr ||
                subId1Argument == nullptr ||
                subId2Argument == nullptr ||
                styleArgument == nullptr ||
                valueArgument == nullptr ||
                !VariantToLong(*dataIdArgument, dataId) ||
                !VariantToLong(*subId1Argument, subId1) ||
                !VariantToLong(*subId2Argument, subId2) ||
                !VariantToLong(*styleArgument, style) ||
                !VariantToString(*valueArgument, value)) {
                return DISP_E_TYPEMISMATCH;
            }
            if (!IsKnownStyle(style)) {
                return SetLongResult(result, static_cast<LONG>(BridgeError::InvalidStyle));
            }

            const auto error = SharedComBridge().Write({static_cast<int>(dataId), static_cast<int>(subId1), static_cast<int>(subId2), ToStyle(style)}, value);
            return SetLongResult(result, static_cast<LONG>(error));
        }
        default:
            return DISP_E_MEMBERNOTFOUND;
        }
    }
};

OBJECT_ENTRY_AUTO(__uuidof(BackendBridgeComClass), CBackendBridge)

/**
 * @brief Process startup arguments and run test/registration/main loops.
 */
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int showCommand)
{
    const std::wstring command = commandLine == nullptr ? L"" : commandLine;
    if (command.find(L"/SelfTest") != std::wstring::npos) {
        return RunSelfTest();
    }
    if (HasArgument(command, L"/RegServer")) {
        return RegisterLocalServer();
    }
    if (HasArgument(command, L"/UnregServer")) {
        return UnregisterLocalServer();
    }
    if (HasArgument(command, L"/ComSelfTest")) {
        return RunComSelfTest();
    }
    return _AtlModule.WinMain(showCommand);
}
