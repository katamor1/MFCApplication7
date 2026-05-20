#pragma once

#include "BackendBridge.h"

#include <atlbase.h>

#include <mutex>

/**
 * @brief COM を介して外部 COM サーバーと通信するバックエンド実装。
 *
 * 接続・読取・更新をスレッドローカルな COM コンテキストで実行します。
 */
class ComBackendBridge final : public IBackendBridge
{
public:
    /**
     * @brief COM 登録済み ProgID を指定して生成する。
     */
    explicit ComBackendBridge(std::wstring progId);
    ~ComBackendBridge() override;

    /**
     * @brief 指定 IP に接続し、COM 側の Connect API を呼ぶ。
     */
    BridgeError Connect(const std::wstring& ipAddress) override;
    /**
     * @brief キー指定で Read API を呼び、文字列値を取得する。
     */
    BridgeError Read(const DataKey& key, std::wstring& value) override;
    /**
     * @brief キー指定で Write API を呼び、値を設定する。
     */
    BridgeError Write(const DataKey& key, const std::wstring& value) override;

private:
    /**
     * @brief IDispatch オブジェクトを初期化し、必要なら事前接続を行う。
     */
    BridgeError EnsureObject(ATL::CComPtr<IDispatch>& dispatch, bool connectIfNeeded);

    /**
     * @brief 接続済み Dispatch へ Connect の Invoke を発行する。
     */
    BridgeError InvokeConnect(IDispatch* dispatch, const std::wstring& ipAddress);

    std::wstring progId_;
    mutable std::mutex connectionMutex_;
    std::wstring connectedIpAddress_;
    bool hasConnectedIpAddress_{false};
};
