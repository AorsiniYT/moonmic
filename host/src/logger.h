#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <filesystem>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif __linux__
#include <unistd.h>
#include <limits.h>
#endif

namespace moonmic {

class Logger {
  public:
    class Line {
      public:
        Line(Logger& logger, bool error) : logger_(&logger), error_(error) {}
        Line(const Line&) = delete;
        Line& operator=(const Line&) = delete;
        Line(Line&& other) noexcept : logger_(other.logger_), error_(other.error_), stream_(std::move(other.stream_)) {
            other.logger_ = nullptr;
        }
        ~Line() {
            if (logger_) logger_->write(stream_.str(), error_);
        }

        template <typename T> Line& operator<<(const T& value) {
            stream_ << value;
            return *this;
        }

        Line& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
            manipulator(stream_);
            return *this;
        }

        Line& operator<<(std::ios_base& (*manipulator)(std::ios_base&)) {
            manipulator(stream_);
            return *this;
        }

      private:
        Logger* logger_;
        bool error_;
        std::ostringstream stream_;
    };

    static Logger& instance() {
        static Logger instance;
        return instance;
    }

    static std::string getLogPath() {
        std::string dir;
#ifdef _WIN32
        char path[MAX_PATH];
        if (GetModuleFileNameA(NULL, path, MAX_PATH) != 0) {
            std::filesystem::path p(path);
            dir = p.parent_path().string();
        }
#elif __linux__
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        if (count != -1) {
            std::filesystem::path p(std::string(result, count));
            dir = p.parent_path().string();
        }
#endif
        if (dir.empty()) dir = ".";

        std::filesystem::path p(dir);
        return (p / "moonmic.log").string();
    }

    bool init(const std::string& logPath = getLogPath()) {
        bool opened = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (logFile_.is_open()) logFile_.close();
            logFile_.open(logPath, std::ios::out | std::ios::trunc);
            opened = logFile_.is_open();
        }
        if (opened)
            out() << "[Logger] Log file opened: " << logPath << std::endl;
        else
            error() << "[Logger] Failed to open log file: " << logPath << std::endl;
        return opened;
    }

    Line out() { return Line(*this, false); }
    Line error() { return Line(*this, true); }

    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (logFile_.is_open()) logFile_.close();
    }

    ~Logger() { close(); }

  private:
    Logger() = default;

    void write(const std::string& message, bool error) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostream& console = error ? std::cerr : std::cout;
        console << message;
        console.flush();
        if (logFile_.is_open()) {
            logFile_ << message;
            logFile_.flush();
        }
    }

    std::mutex mutex_;
    std::ofstream logFile_;
};

inline Logger::Line logInfo() {
    return Logger::instance().out();
}

inline Logger::Line logError() {
    return Logger::instance().error();
}

} // namespace moonmic
