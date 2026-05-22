#include "ExternalProcessLauncher.h"

#include <algorithm>
#include <utility>
#include <windows.h>

/**
 * @file ExternalProcessLauncher.cpp
 * @brief Launches system-screen external applications through Win32 or fake test paths.
 */

namespace {

/**
 * @brief Quote a command-line path for CreateProcess.
 */
std::wstring QuoteCommandPart(const std::wstring& value)
{
    if (value.empty()) {
        return value;
    }
    if (value.front() == L'"' && value.back() == L'"') {
        return value;
    }
    return L"\"" + value + L"\"";
}

/**
 * @brief Build mutable command line text for CreateProcessW.
 */
std::wstring BuildCommandLine(const ExternalAppDefinition& app)
{
    std::wstring command = QuoteCommandPart(app.executablePath);
    if (!app.arguments.empty()) {
        command += L" ";
        command += app.arguments;
    }
    return command;
}

/**
 * @brief Convert Win32 error code to localized message text.
 */
std::wstring FormatWin32Error(unsigned long errorCode)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                            FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        errorCode,
                                        0,
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    if (length == 0 || buffer == nullptr) {
        return L"Win32 error " + std::to_wstring(errorCode);
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L'.')) {
        message.pop_back();
    }
    return message;
}

} // namespace

Win32ExternalProcessLauncher::~Win32ExternalProcessLauncher()
{
    for (const auto& process : runningProcesses_) {
        if (process.processHandle != nullptr) {
            CloseHandle(static_cast<HANDLE>(process.processHandle));
        }
    }
}

void Win32ExternalProcessLauncher::PruneStoppedProcesses()
{
    auto iterator = runningProcesses_.begin();
    while (iterator != runningProcesses_.end()) {
        const auto handle = static_cast<HANDLE>(iterator->processHandle);
        if (handle == nullptr || WaitForSingleObject(handle, 0) != WAIT_TIMEOUT) {
            if (handle != nullptr) {
                CloseHandle(handle);
            }
            iterator = runningProcesses_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

bool Win32ExternalProcessLauncher::IsAlreadyRunning(const std::wstring& appId) const
{
    return std::any_of(runningProcesses_.begin(), runningProcesses_.end(), [&](const RunningProcess& process) {
        return process.appId == appId;
    });
}

ExternalLaunchResult Win32ExternalProcessLauncher::Launch(const ExternalAppDefinition& app)
{
    PruneStoppedProcesses();
    if (!app.allowMultiple && IsAlreadyRunning(app.id)) {
        return {app.id, true, true, 0, L"起動済み"};
    }

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    std::wstring commandLine = BuildCommandLine(app);
    const wchar_t* workingDirectory = app.workingDirectory.empty() ? nullptr : app.workingDirectory.c_str();

    if (CreateProcessW(nullptr,
                       commandLine.data(),
                       nullptr,
                       nullptr,
                       FALSE,
                       0,
                       nullptr,
                       workingDirectory,
                       &startupInfo,
                       &processInfo) == FALSE) {
        const auto errorCode = GetLastError();
        return {app.id, false, false, errorCode, FormatWin32Error(errorCode)};
    }

    if (processInfo.hThread != nullptr) {
        CloseHandle(processInfo.hThread);
    }
    runningProcesses_.push_back({app.id, processInfo.hProcess});
    return {app.id, true, false, 0, L"起動しました"};
}

bool FakeExternalProcessLauncher::IsAlreadyRunning(const std::wstring& appId) const
{
    return std::find(runningAppIds_.begin(), runningAppIds_.end(), appId) != runningAppIds_.end();
}

ExternalLaunchResult FakeExternalProcessLauncher::Launch(const ExternalAppDefinition& app)
{
    if (failNext_) {
        failNext_ = false;
        return {app.id, false, false, nextErrorCode_, nextMessage_};
    }
    if (!app.allowMultiple && IsAlreadyRunning(app.id)) {
        return {app.id, true, true, 0, L"起動済み"};
    }

    runningAppIds_.push_back(app.id);
    return {app.id, true, false, 0, L"起動しました"};
}

void FakeExternalProcessLauncher::FailNext(unsigned long errorCode, std::wstring message)
{
    failNext_ = true;
    nextErrorCode_ = errorCode;
    nextMessage_ = std::move(message);
}
