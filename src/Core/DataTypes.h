#pragma once

#include <chrono>
#include <string>

enum class DataStyle : int
{
    Raw = 0,
    ThousandsSeparated = 1,
    SecondsToHhMmSs = 2,
    MillimetersToInches = 3,
};

enum class BridgeError : int
{
    Ok = 0,
    NotConnected = 1,
    InvalidDataId = 2,
    InvalidSubDataId = 3,
    InvalidStyle = 4,
    ReadOnly = 5,
    Timeout = 6,
    InvalidIpAddress = 7,
    InternalError = 100,
};

struct DataKey
{
    int dataId{};
    int subId1{};
    int subId2{};
    DataStyle style{DataStyle::Raw};

    bool operator==(const DataKey& other) const noexcept;
    bool operator<(const DataKey& other) const noexcept;
};

struct DataValue
{
    std::wstring displayText;
    BridgeError errorCode{BridgeError::Ok};
    std::chrono::steady_clock::time_point updatedAt{};
    bool stale{false};
};

std::wstring ToDisplayText(BridgeError error);
std::wstring ToStyleName(DataStyle style);
DataStyle ParseDataStyleName(const std::string& styleName);
