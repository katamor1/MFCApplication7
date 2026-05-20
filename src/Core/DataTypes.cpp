#include "DataTypes.h"

#include <tuple>

bool DataKey::operator==(const DataKey& other) const noexcept
{
    return dataId == other.dataId && subId1 == other.subId1 && subId2 == other.subId2 && style == other.style;
}

bool DataKey::operator<(const DataKey& other) const noexcept
{
    return std::tie(dataId, subId1, subId2, style) < std::tie(other.dataId, other.subId1, other.subId2, other.style);
}

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
