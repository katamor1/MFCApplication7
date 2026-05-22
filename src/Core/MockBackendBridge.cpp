#include "MockBackendBridge.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>

namespace {

/**
 * @brief Parse integer-like string; fallback 0 on parse errors.
 */
long long ParseInteger(const std::wstring& text)
{
    try {
        return std::stoll(text);
    } catch (...) {
        return 0;
    }
}

std::wstring FormatThousands(long long value)
{
    const bool negative = value < 0;
    if (negative) {
        value = -value;
    }

    auto digits = std::to_wstring(value);
    std::wstring formatted;
    int group = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (group == 3) {
            formatted.insert(formatted.begin(), L',');
            group = 0;
        }
        formatted.insert(formatted.begin(), *it);
        ++group;
    }
    if (negative) {
        formatted.insert(formatted.begin(), L'-');
    }
    return formatted;
}

/**
 * @brief Convert seconds to HH:MM:SS for display.
 */
std::wstring FormatSeconds(long long seconds)
{
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remain = seconds % 60;

    std::wostringstream stream;
    stream << std::setfill(L'0') << std::setw(2) << hours << L':' << std::setw(2) << minutes << L':' << std::setw(2) << remain;
    return stream.str();
}

/**
 * @brief Convert integer millimeters to inch text.
 */
std::wstring FormatInches(long long millimeters)
{
    const double inches = static_cast<double>(millimeters) / 25.4;
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << inches << L" in";
    return stream.str();
}

/**
 * @brief Return pseudo container state for synthetic seed data.
 */
std::wstring ContainerState(int containerNo)
{
    if (containerNo % 29 == 0) {
        return L"コンテナなし";
    }
    if (containerNo % 19 == 0) {
        return L"異常検知";
    }
    if (containerNo % 7 == 0) {
        return L"満載";
    }
    if (containerNo % 5 == 0) {
        return L"追加可能";
    }
    return L"空";
}

/**
 * @brief Return pseudo item count for synthetic seed data.
 */
int ItemCount(int containerNo)
{
    return ContainerState(containerNo) == L"コンテナなし" ? 0 : (containerNo % 10) + 1;
}

/**
 * @brief Decode provisional schedule-add payload: order<TAB>itemName.
 */
bool TryDecodeScheduleAddValue(const std::wstring& value, int& order, std::wstring& itemName)
{
    const auto separator = value.find(L'\t');
    if (separator == std::wstring::npos) {
        return false;
    }
    try {
        order = std::stoi(value.substr(0, separator));
    } catch (...) {
        return false;
    }
    itemName = value.substr(separator + 1);
    return order >= 1 && order <= 9999 && !itemName.empty();
}

} // namespace

/**
 * @file MockBackendBridge.cpp
 * @brief In-process mock backend bridge implementation used for standalone testing.
 */

/**
 * @brief Construct bridge with catalog constraints used during validation.
 */
MockBackendBridge::MockBackendBridge(DataCatalog catalog, MockLoadProfile loadProfile, MockLatencyOptions latencyOptions)
    : catalog_(std::move(catalog))
    , loadProfile_(loadProfile)
    , latencyOptions_(latencyOptions)
{
}

/**
 * @brief Accept only minimally valid dotted IPv4-like addresses.
 */
BridgeError MockBackendBridge::Connect(const std::wstring& ipAddress)
{
    if (ipAddress.empty() || ipAddress.find(L'.') == std::wstring::npos) {
        connected_ = false;
        return BridgeError::InvalidIpAddress;
    }
    connected_ = true;
    return BridgeError::Ok;
}

BridgeError MockBackendBridge::Read(const DataKey& key, std::wstring& value)
{
    if (!connected_.load()) {
        value.clear();
        return BridgeError::NotConnected;
    }

    const auto validation = catalog_.ValidateKey(key);
    if (validation != BridgeError::Ok) {
        value.clear();
        return validation;
    }

    ApplyReadDelay(key);

    std::wstring rawValue;
    bool hasOverride = false;
    {
        std::lock_guard<std::mutex> lock(overridesMutex_);
        const auto overrideValue = overrides_.find(key);
        if (overrideValue != overrides_.end()) {
            rawValue = overrideValue->second;
            hasOverride = true;
        } else if (key.style != DataStyle::Raw) {
            DataKey rawKey = key;
            rawKey.style = DataStyle::Raw;
            const auto rawOverrideValue = overrides_.find(rawKey);
            if (rawOverrideValue != overrides_.end()) {
                rawValue = rawOverrideValue->second;
                hasOverride = true;
            }
        }
    }
    if (!hasOverride) {
        rawValue = RawValue(key);
    }
    value = FormatValue(rawValue, key.style);
    return BridgeError::Ok;
}

/**
 * @brief Write only when connected and key is writable.
 */
BridgeError MockBackendBridge::Write(const DataKey& key, const std::wstring& value)
{
    if (!connected_.load()) {
        return BridgeError::NotConnected;
    }

    const auto validation = catalog_.ValidateKey(key);
    if (validation != BridgeError::Ok) {
        return validation;
    }

    const auto* definition = catalog_.FindDefinition(key.dataId);
    if (definition == nullptr || !definition->writable) {
        return BridgeError::ReadOnly;
    }

    ApplyWriteDelay();

    std::lock_guard<std::mutex> lock(overridesMutex_);
    if (key.dataId == 2104) {
        int order = 0;
        std::wstring itemName;
        if (!TryDecodeScheduleAddValue(value, order, itemName)) {
            return BridgeError::InternalError;
        }

        const DataKey itemNameKey{2100, key.subId1, key.subId2, DataStyle::Raw};
        const DataKey orderKey{2103, key.subId1, key.subId2, DataStyle::Raw};
        const DataKey itemCountKey{2003, key.subId1, 0, DataStyle::Raw};
        overrides_[itemNameKey] = itemName;
        overrides_[orderKey] = std::to_wstring(order);
        const auto itemCount = overrides_.find(itemCountKey);
        const int currentCount = itemCount == overrides_.end() ? SyntheticItemCount(key.subId1) : static_cast<int>(ParseInteger(itemCount->second));
        if (key.subId2 > currentCount) {
            overrides_[itemCountKey] = std::to_wstring(key.subId2);
        }
        overrides_[key] = value;
        return BridgeError::Ok;
    }
    if (key.dataId == 2105) {
        overrides_[{2100, key.subId1, key.subId2, DataStyle::Raw}] = L"";
        overrides_[key] = value;
        return BridgeError::Ok;
    }

    overrides_[key] = value;
    return BridgeError::Ok;
}

/**
 * @brief Generate deterministic synthetic value for testing/preview.
 */
std::wstring MockBackendBridge::RawValue(const DataKey& key) const
{
    if (key.dataId >= 1000 && key.dataId < 1020) {
        switch (key.dataId) {
        case 1000:
            return L"正常";
        case 1001:
            return L"2026/05/20 21:00:00";
        case 1010:
            return L"1234567";
        case 1012:
            return L"3661";
        case 1014:
            return L"2540";
        default:
            return std::to_wstring(100 + key.dataId);
        }
    }

    const int containerNo = key.subId1;
    switch (key.dataId) {
    case 2000:
        return std::to_wstring(containerNo);
    case 2001:
        return L"CNT-" + FormatThousands(containerNo).substr(FormatThousands(containerNo).find_last_of(L",") + 1);
    case 2002:
        return loadProfile_ == MockLoadProfile::MaxLoad ? L"空" : ContainerState(containerNo);
    case 2003:
        return std::to_wstring(SyntheticItemCount(containerNo));
    case 2100:
        return L"ITEM-" + std::to_wstring(containerNo) + L"-" + std::to_wstring(key.subId2);
    case 2101:
        return L"2026/05/" + std::to_wstring((containerNo + key.subId2) % 28 + 1);
    case 2102:
        return L"2026/05/21 " + std::to_wstring((containerNo + key.subId2) % 24) + L":00";
    case 2103:
        return std::to_wstring(containerNo * 10 + key.subId2);
    case 2104:
        return L"";
    case 2105:
        return L"";
    case 2106:
        return std::to_wstring(300 + key.subId2 * 45);
    case 3000:
        return L"2026/05/22 " + std::to_wstring((containerNo + key.subId2) % 24) + L":30";
    case 4000:
        return L"HIST-" + std::to_wstring(key.subId1) + L"-" + std::to_wstring(key.subId2);
    default:
        return L"";
    }
}

/**
 * @brief Apply style conversions for display.
 */
std::wstring MockBackendBridge::FormatValue(const std::wstring& rawValue, DataStyle style) const
{
    switch (style) {
    case DataStyle::Raw:
        return rawValue;
    case DataStyle::ThousandsSeparated:
        return FormatThousands(ParseInteger(rawValue));
    case DataStyle::SecondsToHhMmSs:
        return FormatSeconds(ParseInteger(rawValue));
    case DataStyle::MillimetersToInches:
        return FormatInches(ParseInteger(rawValue));
    default:
        return rawValue;
    }
}

/**
 * @brief Apply configured latency for the read target category.
 */
void MockBackendBridge::ApplyReadDelay(const DataKey& key) const
{
    int delayMs = latencyOptions_.normalReadDelayMs;
    if (key.dataId >= 1000 && key.dataId < 1020) {
        delayMs = latencyOptions_.criticalReadDelayMs;
    } else if (key.dataId == 4000) {
        delayMs = latencyOptions_.historyReadDelayMs;
    }

    if (delayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

/**
 * @brief Apply configured write latency.
 */
void MockBackendBridge::ApplyWriteDelay() const
{
    if (latencyOptions_.writeDelayMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(latencyOptions_.writeDelayMs));
    }
}

/**
 * @brief Return synthetic item count for current load profile.
 */
int MockBackendBridge::SyntheticItemCount(int containerNo) const
{
    return loadProfile_ == MockLoadProfile::MaxLoad ? 10 : ItemCount(containerNo);
}
