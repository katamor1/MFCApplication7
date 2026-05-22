#include "BridgeFactory.h"

#include "ComBackendBridge.h"
#include "MockBackendBridge.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <vector>

/**
 * @file BridgeFactory.cpp
 * @brief Parse startup options and create selected backend bridge implementation.
 */

namespace {

/**
 * @brief Convert option token to lowercase.
 */
std::wstring ToLower(std::wstring value)
{
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return value;
}

/**
 * @brief Extract /Option: value from command line if present.
 */
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

int NonNegativeOptionValue(const std::wstring& commandLine, const wchar_t* optionName)
{
    const auto value = OptionValue(commandLine, optionName);
    if (value.empty()) {
        return 0;
    }
    try {
        return std::max(0, std::stoi(value));
    } catch (...) {
        return 0;
    }
}

} // namespace

/**
 * @brief Load catalog from configured file, fallback to built-in catalog on failure.
 */
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

/**
 * @brief Instantiate COM or mock bridge according to parsed options.
 */
std::shared_ptr<IBackendBridge> CreateBackendBridge(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalogOrDefault(options.catalogPath);
    if (options.bridgeMode == BridgeMode::Com) {
        return std::make_shared<ComBackendBridge>(options.progId);
    }
    return std::make_shared<MockBackendBridge>(catalog, options.mockLoadProfile, options.mockLatencyOptions);
}

/**
 * @brief Parse command-line flags into bridge options.
 */
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

    const auto mockProfile = ToLower(OptionValue(commandLine, L"/MockProfile:"));
    if (mockProfile == L"maxload" || mockProfile == L"max-load") {
        options.mockLoadProfile = MockLoadProfile::MaxLoad;
    } else if (mockProfile == L"default") {
        options.mockLoadProfile = MockLoadProfile::Default;
    }

    options.mockLatencyOptions.criticalReadDelayMs = NonNegativeOptionValue(commandLine, L"/MockCriticalReadDelayMs:");
    options.mockLatencyOptions.normalReadDelayMs = NonNegativeOptionValue(commandLine, L"/MockNormalReadDelayMs:");
    options.mockLatencyOptions.historyReadDelayMs = NonNegativeOptionValue(commandLine, L"/MockHistoryReadDelayMs:");
    options.mockLatencyOptions.writeDelayMs = NonNegativeOptionValue(commandLine, L"/MockWriteDelayMs:");

    return options;
}
