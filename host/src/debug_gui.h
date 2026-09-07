
#pragma once

#include <imgui.h>
#include <deque>
#include <chrono>
#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace moonmic {

struct AudioStats {
    uint64_t packets_received = 0;
    uint64_t packets_dropped = 0;
    uint64_t packets_dropped_lag = 0;
    uint64_t bytes_received = 0;
    std::string last_sender_ip;
    std::string client_name;
    bool is_receiving = false;
    int rtt_ms = -1;
};

enum class GraphInterval { SECONDS_10 = 10, SECONDS_30 = 30, SECONDS_60 = 60 };

struct PerformanceSample {
    float timestamp;
    float packets_per_second;
    float throughput_kbps;
    float cpu_usage_percent;
    float memory_mb;
    float latency_ms;
    float jitter_ms;
    float packet_loss_percent;
};

class PerformanceHistory {
  public:
    PerformanceHistory(int max_samples = 600);

    void addSample(const PerformanceSample& sample);
    void clear();

    std::vector<float> getPacketsPerSecond() const;
    std::vector<float> getThroughputKbps() const;
    std::vector<float> getCpuUsage() const;
    std::vector<float> getMemoryMb() const;
    std::vector<float> getLatency() const;
    std::vector<float> getJitter() const;

    size_t size() const { return samples_.size(); }
    int getMaxSamples() const { return max_samples_; }
    void setMaxSamples(int max) {
        max_samples_ = max;
        trimToSize();
    }

  private:
    void trimToSize();
    std::deque<PerformanceSample> samples_;
    int max_samples_;
};

class AnimatedLED {
  public:
    enum class State { OFF, CONNECTED, ACTIVE, WARNING, LED_ERROR };

    AnimatedLED();

    void setState(State state);
    void update(float delta_time);
    void draw(const char* label, float radius = 8.0f);

    void flash();

  private:
    State state_ = State::OFF;
    float pulse_phase_ = 0.0f;
    float flash_intensity_ = 0.0f;
    std::chrono::steady_clock::time_point last_flash_;
};

class DataFlowIndicator {
  public:
    DataFlowIndicator();

    void update(float delta_time, bool receiving_data);
    void draw(float width = 100.0f, float height = 20.0f);

  private:
    float flow_offset_ = 0.0f;
    float flow_speed_ = 50.0f;
    bool active_ = false;
    float fade_ = 0.0f;
};

class AnimatedGraph {
  public:
    AnimatedGraph(const char* label, const char* unit, ImVec4 color);

    void update(const std::vector<float>& data, float current_value);
    void draw(const ImVec2& size);

    void setRange(float min_val, float max_val);
    void setAutoRange(bool enable) { auto_range_ = enable; }

  private:
    std::string label_;
    std::string unit_;
    ImVec4 color_;
    std::vector<float> display_data_;
    float current_value_ = 0.0f;
    float min_range_ = 0.0f;
    float max_range_ = 100.0f;
    bool auto_range_ = true;
};

class DebugGUI {
  public:
    DebugGUI();
    ~DebugGUI();

    bool initWindow();
    void destroyWindow();

    void update(float delta_time, const AudioStats& stats, bool connected, bool receiving);

    void render();

    void open();
    void close();
    void toggle();
    bool isVisible() const { return visible_; }

    static void showConsole(bool show);
    static bool isConsoleVisible();

    void drawStatusIndicators(bool connected, bool receiving, bool paused = false);

  private:
    void renderContent();
    void renderStatisticsTab();
    void renderPerformanceTab();
    void renderNetworkTab();
    void renderSystemTab();

    void collectSystemMetrics();

    bool visible_ = false;
    bool window_initialized_ = false;

    void* debug_window_ = nullptr;
    void* debug_imgui_context_ = nullptr;

    PerformanceHistory history_;
    GraphInterval graph_interval_ = GraphInterval::SECONDS_30;

    AnimatedLED connection_led_;
    AnimatedLED data_led_;
    DataFlowIndicator data_flow_;

    AnimatedGraph* packets_graph_;
    AnimatedGraph* throughput_graph_;
    AnimatedGraph* cpu_graph_;
    AnimatedGraph* memory_graph_;
    AnimatedGraph* latency_graph_;

    uint64_t last_packets_ = 0;
    uint64_t last_bytes_ = 0;
    float sample_timer_ = 0.0f;
    float sample_interval_ = 0.1f;

    float current_cpu_ = 0.0f;
    float current_memory_ = 0.0f;

    std::chrono::steady_clock::time_point start_time_;
    float uptime_seconds_ = 0.0f;

    float packets_per_second_ = 0.0f;
    float throughput_kbps_ = 0.0f;
    uint64_t total_packets_ = 0;
    uint64_t total_bytes_ = 0;
    uint64_t dropped_packets_ = 0;
    uint64_t dropped_packets_lag_ = 0;
    int current_rtt_ = -1;

#ifdef _WIN32
    HANDLE process_handle_ = nullptr;
    ULARGE_INTEGER last_cpu_time_;
    ULARGE_INTEGER last_sys_time_;
#endif
};

} // namespace moonmic
