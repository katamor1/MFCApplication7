#include "DataCatalog.h"
#include "MockBackendBridge.h"

#include <atlbase.h>
#include <atlcom.h>
#include <oleauto.h>
#include <windows.h>

#include <cwctype>
#include <string>

struct __declspec(uuid("260B8AF6-FD4C-4060-A407-77D52EF54D30")) IBackendBridgeCom : public IDispatch
{
    virtual HRESULT STDMETHODCALLTYPE Connect(BSTR ipAddress, LONG* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Read(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR* value, LONG* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR value, LONG* errorCode) = 0;
};

struct __declspec(uuid("A737D261-91BC-4646-B58E-9E8B53378D6F")) BackendBridge;

class CBackendBridgeMockModule final : public ATL::CAtlExeModuleT<CBackendBridgeMockModule>
{
};

CBackendBridgeMockModule _AtlModule;

namespace {

std::wstring ToLower(std::wstring value)
{
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

bool HasArgument(const std::wstring& command, const wchar_t* argument)
{
    return ToLower(command).find(ToLower(argument)) != std::wstring::npos;
}

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

bool IsKnownStyle(LONG style)
{
    return style >= 0 && style <= 3;
}

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

std::wstring ModulePath()
{
    std::wstring path(MAX_PATH, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(length);
    return path;
}

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

int UnregisterLocalServer()
{
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\MFCApplication7.BackendBridgeMock");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\{A737D261-91BC-4646-B58E-9E8B53378D6F}");
    return 0;
}

} // namespace

class ATL_NO_VTABLE CBackendBridge
    : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>
    , public ATL::CComCoClass<CBackendBridge, &__uuidof(BackendBridge)>
    , public IBackendBridgeCom
{
public:
    CBackendBridge()
        : catalog_(DataCatalog::CreateDefault())
        , bridge_(catalog_)
    {
    }

    DECLARE_NO_REGISTRY()

    BEGIN_COM_MAP(CBackendBridge)
        COM_INTERFACE_ENTRY(IBackendBridgeCom)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()

    HRESULT STDMETHODCALLTYPE Connect(BSTR ipAddress, LONG* errorCode) override
    {
        if (errorCode == nullptr) {
            return E_POINTER;
        }
        *errorCode = static_cast<LONG>(bridge_.Connect(ipAddress == nullptr ? L"" : std::wstring(ipAddress)));
        return S_OK;
    }

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
        const auto error = bridge_.Read({static_cast<int>(dataId), static_cast<int>(subId1), static_cast<int>(subId2), ToStyle(style)}, text);
        SysFreeString(*value);
        *value = SysAllocString(text.c_str());
        *errorCode = static_cast<LONG>(error);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Write(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR value, LONG* errorCode) override
    {
        if (errorCode == nullptr) {
            return E_POINTER;
        }
        if (!IsKnownStyle(style)) {
            *errorCode = static_cast<LONG>(BridgeError::InvalidStyle);
            return S_OK;
        }

        *errorCode = static_cast<LONG>(bridge_.Write({static_cast<int>(dataId), static_cast<int>(subId1), static_cast<int>(subId2), ToStyle(style)},
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

    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override
    {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override
    {
        return E_NOTIMPL;
    }

private:
    DataCatalog catalog_;
    MockBackendBridge bridge_;
};

OBJECT_ENTRY_AUTO(__uuidof(BackendBridge), CBackendBridge)

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
    return _AtlModule.WinMain(showCommand);
}
