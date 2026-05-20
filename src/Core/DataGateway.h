#pragma once

#include "BackendBridge.h"
#include "DataTypes.h"

#include <memory>
#include <string>
#include <vector>

/**
 * @brief 接続済み IBackendBridge を一段抽象したデータアクセス層。
 *
 * Read/Write を呼び出す際の簡易ラッパーとして、更新時刻などの付加情報を保持します。
 */
class DataGateway
{
public:
    /**
     * @brief バックエンドブリッジを所有して構築する。
     */
    explicit DataGateway(std::shared_ptr<IBackendBridge> bridge);

    /**
     * @brief 指定 IP へ接続を委譲する。
     */
    BridgeError Connect(const std::wstring& ipAddress);
    /**
     * @brief 単一キーを読み取り、表示文脈付きで返す。
     */
    DataValue Read(const DataKey& key) const;
    /**
     * @brief 複数キーを順次読み取り、コレクションで返す。
     */
    std::vector<DataValue> ReadMany(const std::vector<DataKey>& keys) const;
    /**
     * @brief 単一キーへ書き込みを委譲する。
     */
    BridgeError Write(const DataKey& key, const std::wstring& value) const;

private:
    std::shared_ptr<IBackendBridge> bridge_;
};
