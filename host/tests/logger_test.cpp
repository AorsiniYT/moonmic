#include "logger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

int main() {
    const auto suffix = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const auto path =
        std::filesystem::temp_directory_path() / ("moonmic-logger-test-" + std::to_string(suffix) + ".log");
    auto& logger = moonmic::Logger::instance();
    if (!logger.init(path.string())) return 1;

    std::vector<std::thread> writers;
    for (int thread = 0; thread < 4; ++thread) {
        writers.emplace_back([thread, &logger]() {
            for (int line = 0; line < 25; ++line) {
                logger.out() << "writer=" << thread << " line=" << line << '\n';
            }
        });
    }
    for (auto& writer : writers)
        writer.join();
    logger.close();

    std::ifstream input(path);
    std::string line;
    int line_count = 0;
    while (std::getline(input, line)) {
        if (line_count > 0 && line.rfind("writer=", 0) != 0) return 2;
        ++line_count;
    }
    input.close();

    std::error_code error;
    std::filesystem::remove(path, error);
    return line_count == 101 && !error ? 0 : 3;
}
