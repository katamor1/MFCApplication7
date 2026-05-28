#include "BridgeFactory.h"

#include "ComBackendBridge.h"
#include "MockBackendBridge.h"

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <utility>
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

std::vector<std::wstring> TokenizeCommandLine(const std::wstring& commandLine)
{
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool inQuotes = false;
    for (size_t index = 0; index < commandLine.size(); ++index) {
        const auto ch = commandLine[index];
        if (ch == L'\\' && index + 1 < commandLine.size() && commandLine[index + 1] == L'"') {
            current.push_back(L'"');
            ++index;
            continue;
        }
        if (ch == L'"') {
            inQuotes = !inQuotes;
            continue;
        }
        if (!inQuotes && std::iswspace(ch)) {
            if (!current.empty()) {
                tokens.push_back(std::move(current));
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

/**
 * @brief Extract /Option: value from command line if present.
 */
std::wstring OptionValue(const std::wstring& commandLine, const wchar_t* optionName)
{
    const std::wstring lowerOption = ToLower(optionName);
    for (const auto& token : TokenizeCommandLine(commandLine)) {
        const auto lowerToken = ToLower(token);
        if (lowerToken.rfind(lowerOption, 0) == 0) {
            return token.substr(lowerOption.size());
        }
    }
    return L"";
}

bool HasOption(const std::wstring& commandLine, const wchar_t* optionName)
{
    const std::wstring lowerOption = ToLower(optionName);
    for (const auto& token : TokenizeCommandLine(commandLine)) {
        const auto lowerToken = ToLower(token);
        if (lowerToken.rfind(lowerOption, 0) == 0) {
            return true;
        }
    }
    return false;
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

DataCatalog LoadConfiguredCatalog(const BridgeFactoryOptions& options)
{
    if (options.catalogPathExplicit) {
        if (options.catalogPath.empty()) {
            throw std::runtime_error("explicit catalog path is empty");
        }
        return DataCatalog::LoadFromFile(options.catalogPath);
    }
    return LoadConfiguredCatalogOrDefault(options.catalogPath);
}

/**
 * @brief Instantiate COM or mock bridge according to parsed options.
 */
std::shared_ptr<IBackendBridge> CreateBackendBridge(const BridgeFactoryOptions& options)
{
    const auto catalog = LoadConfiguredCatalog(options);
    if (options.bridgeMode == BridgeMode::Com) {
        return std::make_shared<ComBackendBridge>(options.progId);
    }
    return std::make_shared<MockBackendBridge>(catalog, options.mockLoadProfile, options.mockLatencyOptions);
}

bool HasCommandLineArgument(const std::wstring& commandLine, const wchar_t* argument)
{
    const auto expected = ToLower(argument);
    for (const auto& token : TokenizeCommandLine(commandLine)) {
        if (ToLower(token) == expected) {
            return true;
        }
    }
    return false;
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
    if (HasOption(commandLine, L"/Catalog:")) {
        options.catalogPathExplicit = true;
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
