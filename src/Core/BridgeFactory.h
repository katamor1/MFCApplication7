#pragma once

#include "BackendBridge.h"
#include "DataCatalog.h"

#include <memory>
#include <string>

enum class BridgeMode
{
    /** 同一プロセス内のモックブリッジを使う。 */
    InProcessMock,
    /** 外部 COM サーバーへ接続する。 */
    Com,
};

struct BridgeFactoryOptions
{
    /** @brief ブリッジ実装の種類。 */
    BridgeMode bridgeMode{BridgeMode::InProcessMock};
    /** @brief COM 実装時に参照する ProgID。 */
    std::wstring progId{L"MFCApplication7.BackendBridgeMock"};
    /** @brief 初期接続先 IP。 */
    std::wstring ipAddress{L"127.0.0.1"};
    /** @brief データカタログ JSON のパス。 */
    std::wstring catalogPath{L"config/data_catalog.json"};
};

/**
 * @brief カタログを読み込み、失敗時は既定値へフォールバックする。
 */
DataCatalog LoadConfiguredCatalogOrDefault(const std::wstring& catalogPath);

/**
 * @brief 起動オプションからバックエンドブリッジ実装を構築する。
 */
std::shared_ptr<IBackendBridge> CreateBackendBridge(const BridgeFactoryOptions& options);

/**
 * @brief コマンドラインから起動オプションを解釈する。
 */
BridgeFactoryOptions ParseBridgeFactoryOptions(const std::wstring& commandLine);
