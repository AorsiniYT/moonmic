#include "guardian_launcher.h"
#include "guardian_state.h"
#include <filesystem>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

namespace moonmic {

#ifdef _WIN32
namespace {
HANDLE shutdown_event = nullptr;
HANDLE restart_event = nullptr;

void closeSynchronizationEvents() {
    if (shutdown_event) {
        CloseHandle(shutdown_event);
        shutdown_event = nullptr;
    }
    if (restart_event) {
        CloseHandle(restart_event);
        restart_event = nullptr;
    }
}
} // namespace
#endif

bool GuardianLauncher::launchGuardian(const std::string& original_mic_id, const std::string& original_mic_name) {

    GuardianState state;
    state.original_mic_id = original_mic_id;
    state.original_mic_name = original_mic_name;
#ifdef _WIN32
    state.host_pid = GetCurrentProcessId();
#else
    state.host_pid = getpid();
#endif
    state.timestamp = time(nullptr);

    if (!GuardianStateManager::writeState(state)) {
        std::cerr << "[GuardianLauncher] Failed to write state" << std::endl;
        return false;
    }

#ifdef _WIN32
    closeSynchronizationEvents();
    shutdown_event = CreateEventA(NULL, TRUE, FALSE, SHUTDOWN_EVENT_NAME);
    restart_event = CreateEventA(NULL, TRUE, FALSE, RESTART_EVENT_NAME);
    if (!shutdown_event || !restart_event) {
        closeSynchronizationEvents();
        GuardianStateManager::deleteState();
        std::cerr << "[GuardianLauncher] Failed to create synchronization events" << std::endl;
        return false;
    }

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::filesystem::path guardianPath = std::filesystem::path(exePath).parent_path() / "moonmic-guardian.exe";

    if (!std::filesystem::exists(guardianPath)) {
        closeSynchronizationEvents();
        GuardianStateManager::deleteState();
        std::cerr << "[GuardianLauncher] Guardian executable not found: " << guardianPath << std::endl;
        return false;
    }

    std::string cmdLine = "\"" + guardianPath.string() + "\" " + std::to_string(GetCurrentProcessId());
    std::vector<char> mutableCmdLine(cmdLine.begin(), cmdLine.end());
    mutableCmdLine.push_back('\0');

    STARTUPINFOA si = {};
    PROCESS_INFORMATION pi = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    if (!CreateProcessA(NULL, mutableCmdLine.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, NULL,
                        &si, &pi)) {
        closeSynchronizationEvents();
        GuardianStateManager::deleteState();
        std::cerr << "[GuardianLauncher] Failed to launch guardian" << std::endl;
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    std::cout << "[GuardianLauncher] Guardian launched (PID: " << pi.dwProcessId << ")" << std::endl;
    return true;
#else

    std::string exePath = std::filesystem::canonical("/proc/self/exe");
    std::filesystem::path guardianPath = std::filesystem::path(exePath).parent_path() / "moonmic-guardian";

    if (!std::filesystem::exists(guardianPath)) {
        GuardianStateManager::deleteState();
        std::cerr << "[GuardianLauncher] Guardian executable not found: " << guardianPath << std::endl;
        return false;
    }

    pid_t pid = fork();
    if (pid == 0) {
        std::string pidStr = std::to_string(state.host_pid);
        execl(guardianPath.c_str(), guardianPath.c_str(), pidStr.c_str(), NULL);
        exit(1);
    } else if (pid > 0) {
        std::cout << "[GuardianLauncher] Guardian launched (PID: " << pid << ")" << std::endl;
        return true;
    } else {
        GuardianStateManager::deleteState();
        std::cerr << "[GuardianLauncher] fork() failed" << std::endl;
        return false;
    }
#endif
}

void GuardianLauncher::signalNormalShutdown() {
#ifdef _WIN32
    if (shutdown_event) {
        SetEvent(shutdown_event);
    } else {
        HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, SHUTDOWN_EVENT_NAME);
        if (event) {
            SetEvent(event);
            CloseHandle(event);
        }
    }
    Sleep(100);
    closeSynchronizationEvents();
#else
    GuardianStateManager::deleteState();
#endif
}

void GuardianLauncher::signalRestart() {
#ifdef _WIN32
    if (restart_event) {
        SetEvent(restart_event);
    } else {
        HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, RESTART_EVENT_NAME);
        if (event) {
            SetEvent(event);
            CloseHandle(event);
        }
    }
    Sleep(100);
    signalNormalShutdown();
#endif
}

bool GuardianLauncher::isGuardianRunning() {
    return GuardianStateManager::stateExists();
}

} // namespace moonmic
