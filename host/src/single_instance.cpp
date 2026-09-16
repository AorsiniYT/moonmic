#include "logger.h"

#include "single_instance.h"
#include <iostream>

#ifdef _WIN32

#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace moonmic {

SingleInstance::SingleInstance(const std::string& app_name)
#ifdef _WIN32
    : mutex_(NULL), window_title_(app_name)
#else
    : lock_fd_(-1), lock_file_("/tmp/" + app_name + ".lock")
#endif
{
#ifdef _WIN32

    std::string mutex_name = "Global\\MoonmicHost_Mutex";
    mutex_ = CreateMutexA(NULL, FALSE, mutex_name.c_str());
#else

    lock_fd_ = open(lock_file_.c_str(), O_CREAT | O_RDWR, 0666);
    if (lock_fd_ != -1) {
        flock(lock_fd_, LOCK_EX | LOCK_NB);
    }
#endif
}

SingleInstance::~SingleInstance() {
#ifdef _WIN32
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
    }
#else
    if (lock_fd_ != -1) {
        flock(lock_fd_, LOCK_UN);
        close(lock_fd_);
        unlink(lock_file_.c_str());
    }
#endif
}

bool SingleInstance::isAnotherInstanceRunning() {
#ifdef _WIN32
    if (mutex_ == NULL) {
        return false;
    }

    DWORD result = WaitForSingleObject(mutex_, 0);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {

        return false;
    }

    moonmic::logInfo() << "[SingleInstance] Mutex held, waiting for release..." << std::endl;
    for (int i = 0; i < 10; i++) {
        Sleep(200);
        result = WaitForSingleObject(mutex_, 0);
        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
            moonmic::logInfo() << "[SingleInstance] Acquired mutex after wait" << std::endl;
            return false;
        }
    }

    return true;
#else
    if (lock_fd_ == -1) {
        return false;
    }

    int result = flock(lock_fd_, LOCK_EX | LOCK_NB);
    if (result == 0) {

        return false;
    }

    return true;
#endif
}

void SingleInstance::bringExistingToFront() {
#ifdef _WIN32

    HWND hwnd = FindWindowA(NULL, window_title_.c_str());
    if (hwnd) {

        if (IsIconic(hwnd)) {
            ShowWindow(hwnd, SW_RESTORE);
        }

        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);

        moonmic::logInfo() << "[SingleInstance] Brought existing window to front" << std::endl;
    }
#else

    moonmic::logInfo() << "[SingleInstance] Another instance is already running" << std::endl;
#endif
}

} // namespace moonmic
