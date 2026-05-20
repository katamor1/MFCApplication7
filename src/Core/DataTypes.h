#pragma once

#include <chrono>
#include <string>

/**
 * @brief バックエンドから取得した値の表示形式。
 *
 * 参照系/設定系 API で style を指定して文字列を変換します。
 */
enum class DataStyle : int
{
    /** 生データ文字列をそのまま返す。 */
    Raw = 0,
    /** 千桁区切り文字列など、3 桁区切りへ変換する。 */
    ThousandsSeparated = 1,
    /** 秒数を hh:mm:ss 形式へ変換する。 */
    SecondsToHhMmSs = 2,
    /** ミリメートル単位をインチ表記へ変換する。 */
    MillimetersToInches = 3,
};

/**
 * @brief バックエンド API のエラーコード。
 *
 * 通常の成功条件は @ref BridgeError::Ok。
 */
enum class BridgeError : int
{
    /** 正常終了。 */
    Ok = 0,
    /** COM 接続未実施または切断。 */
    NotConnected = 1,
    /** データ ID の定義が未登録。 */
    InvalidDataId = 2,
    /** サブ ID の範囲外。 */
    InvalidSubDataId = 3,
    /** 指定した表示形式がデータ定義と不一致。 */
    InvalidStyle = 4,
    /** 参照専用項目へ設定を要求した。 */
    ReadOnly = 5,
    /** 通信がタイムアウト。 */
    Timeout = 6,
    /** IP アドレスの検証に失敗。 */
    InvalidIpAddress = 7,
    /** 予期しない内部エラー。 */
    InternalError = 100,
};

/**
 * @brief データ参照/設定に必要なキー。
 */
struct DataKey
{
    int dataId{};
    int subId1{};
    int subId2{};
    DataStyle style{DataStyle::Raw};

    /**
     * @brief 全フィールドが一致するか比較する。
     */
    bool operator==(const DataKey& other) const noexcept;

    /**
     * @brief map/set 用の辞書式比較。
     */
    bool operator<(const DataKey& other) const noexcept;
};

/**
 * @brief 表示に使う単位付きデータ値。
 */
struct DataValue
{
    std::wstring displayText;
    BridgeError errorCode{BridgeError::Ok};
    std::chrono::steady_clock::time_point updatedAt{};
    bool stale{false};
};

/**
 * @brief エラーコードを画面表示用文字列へ変換する。
 */
std::wstring ToDisplayText(BridgeError error);

/**
 * @brief 表示スタイル列挙値を短い文字列へ変換する。
 */
std::wstring ToStyleName(DataStyle style);

/**
 * @brief JSON 設定文字列から DataStyle を解決する。
 * @param styleName raw / thousands / hhmmss / inch
 */
DataStyle ParseDataStyleName(const std::string& styleName);
