#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <filesystem>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#elif __linux__
#include <unistd.h>
#include <limits.h>
#endif

namespace moonmic {

class Logger {
  public:
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

    void init() {
        std::string logPath = getLogPath();

        logFile_.open(logPath, std::ios::out | std::ios::trunc);
        if (logFile_.is_open()) {
            original_cout_ = std::cout.rdbuf();
            original_cerr_ = std::cerr.rdbuf();
            cout_tee_ = std::make_unique<TeeStreamBuf>(original_cout_, logFile_);
            cerr_tee_ = std::make_unique<TeeStreamBuf>(original_cerr_, logFile_);

            std::cout.rdbuf(cout_tee_.get());
            std::cerr.rdbuf(cerr_tee_.get());

            std::cout << "[Logger] Log file opened: " << logPath << std::endl;
        } else {
            std::cerr << "[Logger] Failed to open log file: " << logPath << std::endl;
        }
    }

    ~Logger() {
        if (logFile_.is_open()) {
            std::cout.rdbuf(original_cout_);
            std::cerr.rdbuf(original_cerr_);
            logFile_.close();
        }
    }

  private:
    Logger() = default;

    std::ofstream logFile_;
    std::streambuf* original_cout_ = nullptr;
    std::streambuf* original_cerr_ = nullptr;

    class TeeStreamBuf : public std::streambuf {
      public:
        TeeStreamBuf(std::streambuf* sb1, std::ostream& os2) : sb1_(sb1), os2_(os2) {}

      protected:
        virtual int overflow(int c) override {
            if (c == EOF) return !EOF;
            int const r1 = sb1_->sputc(c);
            os2_.put(c);
            return r1;
        }

        virtual int sync() override {
            int const r1 = sb1_->pubsync();
            os2_.flush();
            return r1;
        }

      private:
        std::streambuf* sb1_;
        std::ostream& os2_;
    };

    std::unique_ptr<TeeStreamBuf> cout_tee_;
    std::unique_ptr<TeeStreamBuf> cerr_tee_;
};

} // namespace moonmic
