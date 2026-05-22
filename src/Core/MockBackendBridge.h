#pragma once

#include "BackendBridge.h"
#include "DataCatalog.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>

enum class MockLoadProfile
{
    /** 現行互換の軽量モックデータ。 */
    Default,
    /** 100コンテナ/1000品目相当の最大負荷データ。 */
    MaxLoad,
};

struct MockLatencyOptions
{
    /** 重要情報Readごとの遅延ミリ秒。 */
    int criticalReadDelayMs{};
    /** 通常/スケジュールReadごとの遅延ミリ秒。 */
    int normalReadDelayMs{};
    /** 履歴Readごとの遅延ミリ秒。 */
    int historyReadDelayMs{};
    /** Write呼び出しごとの遅延ミリ秒。 */
    int writeDelayMs{};
};

/**
 * @brief モックバックエンド実装（ローカル辞書 + 規則生成データ）。
 *
 * 開発・テスト時の代替実装として使用し、実 COM 未接続でも動作確認可能にする。
 */
class MockBackendBridge final : public IBackendBridge
{
public:
    /**
     * @brief コンストラクタ。カタログ定義を受け取りキー検証の基準を持つ。
     */
    explicit MockBackendBridge(DataCatalog catalog,
                               MockLoadProfile loadProfile = MockLoadProfile::Default,
                               MockLatencyOptions latencyOptions = {});

    /**
     * @brief IP 形式の簡易検証後、内部状態を接続済みに更新する。
     */
    BridgeError Connect(const std::wstring& ipAddress) override;
    /**
     * @brief 仮データを取得し、必要に応じて表記変換を適用する。
     */
    BridgeError Read(const DataKey& key, std::wstring& value) override;
    /**
     * @brief 参照可/不可定義を確認し、上書き値を保持する。
     */
    BridgeError Write(const DataKey& key, const std::wstring& value) override;

private:
    /**
     * @brief スタイル変換前の素データを生成する。
     */
    std::wstring RawValue(const DataKey& key) const;

    /**
     * @brief Raw 文字列を style 指定で変換する。
     */
    std::wstring FormatValue(const std::wstring& rawValue, DataStyle style) const;

    /** @brief キー種別に応じたRead遅延を適用する。 */
    void ApplyReadDelay(const DataKey& key) const;
    /** @brief Write遅延を適用する。 */
    void ApplyWriteDelay() const;
    /** @brief 負荷プロファイルに応じた品目数を返す。 */
    int SyntheticItemCount(int containerNo) const;

    DataCatalog catalog_;
    MockLoadProfile loadProfile_{MockLoadProfile::Default};
    MockLatencyOptions latencyOptions_;
    std::atomic<bool> connected_{false};
    mutable std::mutex overridesMutex_;
    std::map<DataKey, std::wstring> overrides_;
};
