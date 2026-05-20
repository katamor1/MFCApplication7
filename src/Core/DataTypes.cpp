#include "DataTypes.h"

#include <stdexcept>
#include <tuple>

/**
 * @file DataTypes.cpp
 * @brief Type-level helpers for data keys and style/error conversion helpers.
 */

/**
 * @brief Compare two data keys for equality.
 * @param other Comparison target.
 * @return true when all key fields match.
 */
bool DataKey::operator==(const DataKey& other) const noexcept
{
    return dataId == other.dataId && subId1 == other.subId1 && subId2 == other.subId2 && style == other.style;
}

/**
 * @brief Provide a stable strict weak ordering for use in ordered containers.
 * @param other Comparison target.
 * @return true when this key should be ordered before other.
 */
bool DataKey::operator<(const DataKey& other) const noexcept
{
    return std::tie(dataId, subId1, subId2, style) < std::tie(other.dataId, other.subId1, other.subId2, other.style);
}

/**
 * @brief Convert bridge error code to a localized, UI-facing text.
 * @param error Bridge error value.
 * @return Human-readable status message.
 */
std::wstring ToDisplayText(BridgeError error)
{
    switch (error) {
    case BridgeError::Ok:
        return L"OK";
    case BridgeError::NotConnected:
        return L"COM未接続";
    case BridgeError::InvalidDataId:
        return L"不正データID";
    case BridgeError::InvalidSubDataId:
        return L"不正サブデータID";
    case BridgeError::InvalidStyle:
        return L"不正データスタイル";
    case BridgeError::ReadOnly:
        return L"設定不可";
    case BridgeError::Timeout:
        return L"タイムアウト";
    case BridgeError::InvalidIpAddress:
        return L"不正IP";
    default:
        return L"内部エラー";
    }
}

/**
 * @brief Convert data style enum to a compact wire/table style identifier.
 * @param style Style enum to convert.
 * @return Name used by screens, configuration, and logs.
 */
std::wstring ToStyleName(DataStyle style)
{
    switch (style) {
    case DataStyle::Raw:
        return L"raw";
    case DataStyle::ThousandsSeparated:
        return L"thousands";
    case DataStyle::SecondsToHhMmSs:
        return L"hhmmss";
    case DataStyle::MillimetersToInches:
        return L"inch";
    default:
        return L"unknown";
    }
}

/**
 * @brief Parse style name string to enum.
 * @param styleName Lowercase text from config/CLI/UI.
 * @return Matching DataStyle enum value.
 * @throw std::runtime_error on unknown value.
 */
DataStyle ParseDataStyleName(const std::string& styleName)
{
    if (styleName == "raw") {
        return DataStyle::Raw;
    }
    if (styleName == "thousands") {
        return DataStyle::ThousandsSeparated;
    }
    if (styleName == "hhmmss") {
        return DataStyle::SecondsToHhMmSs;
    }
    if (styleName == "inch") {
        return DataStyle::MillimetersToInches;
    }
    throw std::runtime_error("unknown data style: " + styleName);
}
