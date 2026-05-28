#pragma once

#include "ScreenModels.h"

#include <string>
#include <vector>

/**
 * @brief 外部プロセス起動境界。
 */
class IExternalProcessLauncher
{
public:
    virtual ~IExternalProcessLauncher() = default;

    /**
     * @brief 外部アプリ定義に従って起動を試みる。
     */
    virtual ExternalLaunchResult Launch(const ExternalAppDefinition& app) = 0;
};

/**
 * @brief 外部アプリの実行ファイルパスをアプリ本体の配置ディレクトリ基準で絶対化する。
 */
std::wstring ResolveExternalExecutablePathForLaunch(const ExternalAppDefinition& app, const std::wstring& modulePath);

/**
 * @brief Win32 API で外部プロセスを起動する実装。
 */
class Win32ExternalProcessLauncher final : public IExternalProcessLauncher
{
public:
    ~Win32ExternalProcessLauncher() override;

    ExternalLaunchResult Launch(const ExternalAppDefinition& app) override;

private:
    struct RunningProcess
    {
        std::wstring appId;
        void* processHandle{};
    };

    void PruneStoppedProcesses();
    bool IsAlreadyRunning(const std::wstring& appId) const;

    std::vector<RunningProcess> runningProcesses_;
};

/**
 * @brief 自己診断用の副作用なし外部プロセス起動器。
 */
class FakeExternalProcessLauncher final : public IExternalProcessLauncher
{
public:
    ExternalLaunchResult Launch(const ExternalAppDefinition& app) override;
    void FailNext(unsigned long errorCode, std::wstring message);

private:
    bool IsAlreadyRunning(const std::wstring& appId) const;

    std::vector<std::wstring> runningAppIds_;
    bool failNext_{false};
    unsigned long nextErrorCode_{};
    std::wstring nextMessage_;
};
