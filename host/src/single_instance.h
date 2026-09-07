
#pragma once

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace moonmic {

class SingleInstance {
  public:
    SingleInstance(const std::string& app_name);
    ~SingleInstance();

    bool isAnotherInstanceRunning();

    void bringExistingToFront();

  private:
#ifdef _WIN32
    HANDLE mutex_;
    std::string window_title_;
#else
    int lock_fd_;
    std::string lock_file_;
#endif
};

} // namespace moonmic
