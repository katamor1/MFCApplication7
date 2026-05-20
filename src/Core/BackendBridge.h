#pragma once

#include "DataTypes.h"

#include <string>
#include <winsock2.h>
#include <windows.h>
#include <oaidl.h>

/**
 * @brief COM 経由で外部ブリッジへ接続するインターフェイス。
 *
 * 1 つの接続先に対して Read / Write を実行します。
 */
struct __declspec(uuid("260B8AF6-FD4C-4060-A407-77D52EF54D30")) IBackendBridgeCom : public IDispatch
{
    virtual HRESULT STDMETHODCALLTYPE Connect(BSTR ipAddress, LONG* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Read(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR* value, LONG* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Write(LONG dataId, LONG subId1, LONG subId2, LONG style, BSTR value, LONG* errorCode) = 0;
};

struct __declspec(uuid("A737D261-91BC-4646-B58E-9E8B53378D6F")) BackendBridgeComClass;

/**
 * @brief アプリ本体が利用するバックエンド接続の抽象インターフェイス。
 */
class IBackendBridge
{
public:
    virtual ~IBackendBridge() = default;

    /**
     * @brief 指定した IP へ接続する。
     * @param ipAddress 接続先 IP
     */
    virtual BridgeError Connect(const std::wstring& ipAddress) = 0;

    /**
     * @brief データキーに対する文字列値を1件取得する。
     * @param key 取得対象キー
     * @param value 取得値（Out）
     */
    virtual BridgeError Read(const DataKey& key, std::wstring& value) = 0;

    /**
     * @brief データキーへ文字列値を設定する。
     * @param key 設定対象キー
     * @param value 設定する値
     */
    virtual BridgeError Write(const DataKey& key, const std::wstring& value) = 0;
};
