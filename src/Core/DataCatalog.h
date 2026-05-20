#pragma once

#include "DataTypes.h"

#include <string>
#include <vector>

/**
 * @brief 1件のデータ定義を保持するモデル。
 */
struct DataDefinition
{
    /** データID。 */
    int dataId{};
    /** 表示名（日本語表記）。 */
    std::wstring name;
    /** 設定可能フラグ。 */
    bool writable{false};
    /** サブID1 の最小/最大。 */
    int minSubId1{};
    int maxSubId1{};
    /** サブID2 の最小/最大。 */
    int minSubId2{};
    int maxSubId2{};
    /** 使用可能な表示スタイル。 */
    std::vector<DataStyle> allowedStyles;
};

/**
 * @brief データID カタログを保持し、参照/検証を提供する。
 */
class DataCatalog
{
public:
    /**
     * @brief 組み込みの既定カタログを生成する。
     */
    static DataCatalog CreateDefault();

    /**
     * @brief 外部 JSON ファイルからカタログを読み込む。
     * @throws std::runtime_error 読み込み/形式エラー時
     */
    static DataCatalog LoadFromFile(const std::wstring& path);

    /** 定義全件を返す。 */
    const std::vector<DataDefinition>& Definitions() const noexcept;
    /** 重要監視キー全件を返す。 */
    const std::vector<DataKey>& CriticalKeys() const noexcept;
    /** データID から定義を検索する。 */
    const DataDefinition* FindDefinition(int dataId) const noexcept;
    /**
     * @brief 指定データIDに対して style が許可されているか。
     */
    bool IsStyleAllowed(int dataId, DataStyle style) const noexcept;
    /**
     * @brief サブ ID / スタイルの妥当性を検証する。
     */
    BridgeError ValidateKey(const DataKey& key) const noexcept;
    /**
     * @brief 指定データIDが更新可否を返す。
     */
    bool IsWritable(int dataId) const noexcept;

private:
    /**
     * @brief 既存定義へ1件追加する。
     */
    void AddDefinition(DataDefinition definition);
    /**
     * @brief 監視用キーを1件追加する。
     */
    void AddCriticalKey(DataKey key);

    std::vector<DataDefinition> definitions_;
    std::vector<DataKey> criticalKeys_;
};
