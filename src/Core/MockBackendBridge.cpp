#include "MockBackendBridge.h"

#include <iomanip>
#include <shared_mutex>
#include <sstream>

namespace {

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

std::wstring FormatSeconds(long long seconds)
{
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remain = seconds % 60;

    std::wostringstream stream;
    stream << std::setfill(L'0') << std::setw(2) << hours << L':' << std::setw(2) << minutes << L':' << std::setw(2) << remain;
    return stream.str();
}

std::wstring FormatInches(long long millimeters)
{
    const double inches = static_cast<double>(millimeters) / 25.4;
    std::wostringstream stream;
    stream << std::fixed << std::setprecision(2) << inches << L" in";
    return stream.str();
}

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

int ItemCount(int containerNo)
{
    return ContainerState(containerNo) == L"コンテナなし" ? 0 : (containerNo % 10) + 1;
}

} // namespace

MockBackendBridge::MockBackendBridge(DataCatalog catalog)
    : catalog_(std::move(catalog))
{
}

BridgeError MockBackendBridge::Connect(const std::wstring& ipAddress)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (ipAddress.empty() || ipAddress.find(L'.') == std::wstring::npos) {
        connected_ = false;
        return BridgeError::InvalidIpAddress;
    }
    connected_ = true;
    return BridgeError::Ok;
}

BridgeError MockBackendBridge::Read(const DataKey& key, std::wstring& value)
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (!connected_) {
        value.clear();
        return BridgeError::NotConnected;
    }

    const auto validation = catalog_.ValidateKey(key);
    if (validation != BridgeError::Ok) {
        value.clear();
        return validation;
    }

    const auto overrideValue = overrides_.find(key);
    const auto rawValue = overrideValue == overrides_.end() ? RawValue(key) : overrideValue->second;
    value = FormatValue(rawValue, key.style);
    return BridgeError::Ok;
}

BridgeError MockBackendBridge::Write(const DataKey& key, const std::wstring& value)
{
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (!connected_) {
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

    overrides_[key] = value;
    return BridgeError::Ok;
}

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
        return ContainerState(containerNo);
    case 2003:
        return std::to_wstring(ItemCount(containerNo));
    case 2100:
        return L"ITEM-" + std::to_wstring(containerNo) + L"-" + std::to_wstring(key.subId2);
    case 2101:
        return L"2026/05/" + std::to_wstring((containerNo + key.subId2) % 28 + 1);
    case 2102:
        return L"2026/05/21 " + std::to_wstring((containerNo + key.subId2) % 24) + L":00";
    case 2103:
        return std::to_wstring(containerNo * 10 + key.subId2);
    case 2104:
        return std::to_wstring(300 + key.subId2 * 45);
    case 3000:
        return L"2026/05/22 " + std::to_wstring((containerNo + key.subId2) % 24) + L":30";
    case 4000:
        return L"HIST-" + std::to_wstring(key.subId1) + L"-" + std::to_wstring(key.subId2);
    default:
        return L"";
    }
}

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
