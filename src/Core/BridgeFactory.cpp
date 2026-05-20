#include "BridgeFactory.h"

#include "ComBackendBridge.h"
#include "MockBackendBridge.h"

#include <cwctype>
#include <stdexcept>
#include <vector>

namespace {

std::wstring ToLower(std::wstring value)
{
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

std::wstring OptionValue(const std::wstring& commandLine, const wchar_t* optionName)
{
    const std::wstring lowerCommand = ToLower(commandLine);
    const std::wstring lowerOption = ToLower(optionName);
    const auto optionStart = lowerCommand.find(lowerOption);
    if (optionStart == std::wstring::npos) {
        return L"";
    }

    const auto valueStart = optionStart + lowerOption.size();
    auto valueEnd = commandLine.find(L' ', valueStart);
    if (valueEnd == std::wstring::npos) {
        valueEnd = commandLine.size();
    }
    auto value = commandLine.substr(valueStart, valueEnd - valueStart);
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

} // namespace

DataCatalog LoadConfiguredCatalogOrDefault(const std::wstring& catalogPath)
{
    if (!catalogPath.empty()) {
        try {
            return DataCatalog::LoadFromFile(catalogPath);
        } catch (const std::exception&) {
        }
    }
    return DataCatalog::CreateDefault();
}

std::shared_ptr<IBackendBridge> CreateBackendBridge(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    if (options.bridgeMode == BridgeMode::Com) {
        return std::make_shared<ComBackendBridge>(options.progId);
    }
    return std::make_shared<MockBackendBridge>(catalog);
}

BridgeFactoryOptions ParseBridgeFactoryOptions(const std::wstring& commandLine)
{
    BridgeFactoryOptions options;

    const auto bridge = ToLower(OptionValue(commandLine, L"/Bridge:"));
    if (bridge == L"com") {
        options.bridgeMode = BridgeMode::Com;
    } else if (bridge == L"mock") {
        options.bridgeMode = BridgeMode::InProcessMock;
    }

    const auto progId = OptionValue(commandLine, L"/ProgId:");
    if (!progId.empty()) {
        options.progId = progId;
    }

    const auto ip = OptionValue(commandLine, L"/Ip:");
    if (!ip.empty()) {
        options.ipAddress = ip;
    }

    const auto catalog = OptionValue(commandLine, L"/Catalog:");
    if (!catalog.empty()) {
        options.catalogPath = catalog;
    }

    return options;
}
