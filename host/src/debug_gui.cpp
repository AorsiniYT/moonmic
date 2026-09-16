#include "logger.h"

#include "debug_gui.h"
#include "audio_receiver.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace moonmic {

PerformanceHistory::PerformanceHistory(int max_samples) : max_samples_(max_samples) {
}

void PerformanceHistory::addSample(const PerformanceSample& sample) {
    samples_.push_back(sample);
    trimToSize();
}

void PerformanceHistory::clear() {
    samples_.clear();
}

void PerformanceHistory::trimToSize() {
    while (samples_.size() > static_cast<size_t>(max_samples_)) {
        samples_.pop_front();
    }
}

std::vector<float> PerformanceHistory::getPacketsPerSecond() const {
    std::vector<float> data;
    data.reserve(samples_.size());
    for (const auto& s : samples_)
        data.push_back(s.packets_per_second);
    return data;
}

std::vector<float> PerformanceHistory::getThroughputKbps() const {
    std::vector<float> data;
    data.reserve(samples_.size());
    for (const auto& s : samples_)
        data.push_back(s.throughput_kbps);
    return data;
}

std::vector<float> PerformanceHistory::getCpuUsage() const {
    std::vector<float> data;
    data.reserve(samples_.size());
    for (const auto& s : samples_)
        data.push_back(s.cpu_usage_percent);
    return data;
}

std::vector<float> PerformanceHistory::getMemoryMb() const {
    std::vector<float> data;
    data.reserve(samples_.size());
    for (const auto& s : samples_)
        data.push_back(s.memory_mb);
    return data;
}

std::vector<float> PerformanceHistory::getLatency() const {
    std::vector<float> data;
    data.reserve(samples_.size());
    for (const auto& s : samples_)
        data.push_back(s.latency_ms);
    return data;
}

std::vector<float> PerformanceHistory::getJitter() const {
    std::vector<float> data;
    data.reserve(samples_.size());
    for (const auto& s : samples_)
        data.push_back(s.jitter_ms);
    return data;
}

AnimatedLED::AnimatedLED() : last_flash_(std::chrono::steady_clock::now()) {
}

void AnimatedLED::setState(State state) {
    state_ = state;
}

void AnimatedLED::update(float delta_time) {

    pulse_phase_ += delta_time * 4.0f;
    if (pulse_phase_ > 2.0f * M_PI) {
        pulse_phase_ -= 2.0f * M_PI;
    }

    flash_intensity_ = std::max(0.0f, flash_intensity_ - delta_time * 5.0f);
}

void AnimatedLED::flash() {
    flash_intensity_ = 1.0f;
    last_flash_ = std::chrono::steady_clock::now();
}

void AnimatedLED::draw(const char* label, float radius) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImVec2 center = ImVec2(pos.x + radius + 2, pos.y + radius + 2);

    ImVec4 base_color;
    switch (state_) {
    case State::OFF:
        base_color = ImVec4(0.3f, 0.3f, 0.3f, 1.0f);
        break;
    case State::CONNECTED:
        base_color = ImVec4(0.0f, 0.7f, 0.0f, 1.0f);
        break;
    case State::ACTIVE:

    {
        float pulse = 0.5f + 0.5f * sinf(pulse_phase_);
        base_color = ImVec4(0.0f, 0.5f + 0.5f * pulse, 0.0f, 1.0f);
    } break;
    case State::WARNING:
        base_color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        break;
    case State::LED_ERROR:
        base_color = ImVec4(0.9f, 0.1f, 0.1f, 1.0f);
        break;
    }

    if (flash_intensity_ > 0.0f) {
        base_color.x = std::min(1.0f, base_color.x + flash_intensity_ * 0.5f);
        base_color.y = std::min(1.0f, base_color.y + flash_intensity_ * 0.5f);
        base_color.z = std::min(1.0f, base_color.z + flash_intensity_ * 0.5f);
    }

    ImU32 color = ImGui::ColorConvertFloat4ToU32(base_color);

    if (state_ != State::OFF) {
        ImU32 glow_color = ImGui::ColorConvertFloat4ToU32(ImVec4(base_color.x, base_color.y, base_color.z, 0.3f));
        draw_list->AddCircleFilled(center, radius * 1.5f, glow_color);
    }

    draw_list->AddCircleFilled(center, radius, color);

    ImU32 highlight = IM_COL32(255, 255, 255, 80);
    draw_list->AddCircleFilled(ImVec2(center.x - radius * 0.3f, center.y - radius * 0.3f), radius * 0.4f, highlight);

    draw_list->AddCircle(center, radius, IM_COL32(60, 60, 60, 255), 0, 2.0f);

    ImGui::Dummy(ImVec2(radius * 2 + 4, radius * 2 + 4));
    ImGui::SameLine();
    ImGui::Text("%s", label);
}

DataFlowIndicator::DataFlowIndicator() {
}

void DataFlowIndicator::update(float delta_time, bool receiving_data) {
    active_ = receiving_data;

    if (active_) {
        flow_offset_ += flow_speed_ * delta_time;
        if (flow_offset_ > 20.0f) flow_offset_ -= 20.0f;
        fade_ = std::min(1.0f, fade_ + delta_time * 5.0f);
    } else {
        fade_ = std::max(0.0f, fade_ - delta_time * 2.0f);
    }
}

void DataFlowIndicator::draw(float width, float height) {
    if (fade_ <= 0.01f) {
        ImGui::Dummy(ImVec2(width, height));
        return;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(30, 30, 30, 255), 4.0f);

    float packet_size = 6.0f;
    float spacing = 20.0f;

    for (float x = -flow_offset_; x < width; x += spacing) {
        if (x > 0 && x < width - packet_size) {
            float local_fade = 1.0f;

            if (x < 20) local_fade = x / 20.0f;
            if (x > width - 20) local_fade = (width - x) / 20.0f;

            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.8f, 0.4f, fade_ * local_fade));

            draw_list->AddRectFilled(ImVec2(pos.x + x, pos.y + height / 2 - packet_size / 2),
                                     ImVec2(pos.x + x + packet_size, pos.y + height / 2 + packet_size / 2), col, 2.0f);
        }
    }

    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(60, 60, 60, 255), 4.0f);

    ImGui::Dummy(ImVec2(width, height));
}

AnimatedGraph::AnimatedGraph(const char* label, const char* unit, ImVec4 color)
    : label_(label), unit_(unit), color_(color) {
}

void AnimatedGraph::update(const std::vector<float>& data, float current_value) {
    display_data_ = data;
    current_value_ = current_value;

    if (auto_range_ && !data.empty()) {
        float max_val = *std::max_element(data.begin(), data.end());
        max_range_ = std::max(max_range_ * 0.95f, max_val * 1.2f);
        if (max_range_ < 1.0f) max_range_ = 1.0f;
    }
}

void AnimatedGraph::setRange(float min_val, float max_val) {
    min_range_ = min_val;
    max_range_ = max_val;
    auto_range_ = false;
}

void AnimatedGraph::draw(const ImVec2& size) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(20, 20, 25, 255), 4.0f);

    for (int i = 1; i < 4; i++) {
        float y = pos.y + size.y * (i / 4.0f);
        draw_list->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + size.x, y), IM_COL32(50, 50, 60, 100));
    }

    if (display_data_.size() > 1 && max_range_ > min_range_) {

        float data_count = static_cast<float>(display_data_.size());
        float x_step = size.x / (data_count - 1);

        ImU32 fill_color = ImGui::ColorConvertFloat4ToU32(ImVec4(color_.x, color_.y, color_.z, 0.2f));
        ImU32 line_color = ImGui::ColorConvertFloat4ToU32(color_);

        float bottom_y = pos.y + size.y;

        for (size_t i = 0; i < display_data_.size() - 1; i++) {
            float normalized1 = (display_data_[i] - min_range_) / (max_range_ - min_range_);
            float normalized2 = (display_data_[i + 1] - min_range_) / (max_range_ - min_range_);
            normalized1 = std::clamp(normalized1, 0.0f, 1.0f);
            normalized2 = std::clamp(normalized2, 0.0f, 1.0f);

            float x1 = pos.x + i * x_step;
            float x2 = pos.x + (i + 1) * x_step;
            float y1 = pos.y + size.y * (1.0f - normalized1);
            float y2 = pos.y + size.y * (1.0f - normalized2);

            draw_list->AddTriangleFilled(ImVec2(x1, y1), ImVec2(x2, y2), ImVec2(x2, bottom_y), fill_color);

            draw_list->AddTriangleFilled(ImVec2(x1, y1), ImVec2(x2, bottom_y), ImVec2(x1, bottom_y), fill_color);

            draw_list->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), line_color, 2.0f);
        }

        if (!display_data_.empty()) {
            float last_x = pos.x + (display_data_.size() - 1) * x_step;
            float last_normalized = (display_data_.back() - min_range_) / (max_range_ - min_range_);
            last_normalized = std::clamp(last_normalized, 0.0f, 1.0f);
            ImVec2 dot_pos(last_x, pos.y + size.y * (1.0f - last_normalized));
            draw_list->AddCircleFilled(dot_pos, 4.0f, line_color);
            draw_list->AddCircle(dot_pos, 6.0f, IM_COL32(255, 255, 255, 100));
        }
    }

    draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(60, 60, 70, 255), 4.0f);

    std::stringstream ss;
    ss << std::fixed << std::setprecision(1) << current_value_ << " " << unit_;

    ImGui::SetCursorScreenPos(ImVec2(pos.x + 8, pos.y + 4));
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", label_.c_str());

    ImGui::SetCursorScreenPos(ImVec2(pos.x + size.x - 80, pos.y + 4));
    ImGui::TextColored(color_, "%s", ss.str().c_str());

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + 4));
}

DebugGUI::DebugGUI() : history_(300), start_time_(std::chrono::steady_clock::now()) {

    packets_graph_ = new AnimatedGraph("Packets/s", "pps", ImVec4(0.2f, 0.8f, 0.4f, 1.0f));
    throughput_graph_ = new AnimatedGraph("Throughput", "KB/s", ImVec4(0.4f, 0.6f, 1.0f, 1.0f));
    cpu_graph_ = new AnimatedGraph("CPU", "%", ImVec4(1.0f, 0.6f, 0.2f, 1.0f));
    memory_graph_ = new AnimatedGraph("Memory", "MB", ImVec4(0.8f, 0.3f, 0.8f, 1.0f));
    latency_graph_ = new AnimatedGraph("Latency", "ms", ImVec4(0.9f, 0.9f, 0.2f, 1.0f));

    cpu_graph_->setRange(0, 100);

#ifdef _WIN32
    process_handle_ = GetCurrentProcess();
    FILETIME create, exit, kernel, user;
    GetProcessTimes(process_handle_, &create, &exit, &kernel, &user);
    last_cpu_time_.LowPart = user.dwLowDateTime;
    last_cpu_time_.HighPart = user.dwHighDateTime;

    FILETIME idle, kern, usr;
    GetSystemTimes(&idle, &kern, &usr);
    last_sys_time_.LowPart = usr.dwLowDateTime;
    last_sys_time_.HighPart = usr.dwHighDateTime;
#endif
}

DebugGUI::~DebugGUI() {
    destroyWindow();
    delete packets_graph_;
    delete throughput_graph_;
    delete cpu_graph_;
    delete memory_graph_;
    delete latency_graph_;
}

void DebugGUI::showConsole(bool show) {
#ifdef _WIN32
    HWND console = GetConsoleWindow();
    if (console) {
        ShowWindow(console, show ? SW_SHOW : SW_HIDE);
    }
#endif
}

bool DebugGUI::isConsoleVisible() {
#ifdef _WIN32
    HWND console = GetConsoleWindow();
    if (console) {
        return IsWindowVisible(console);
    }
#endif
    return false;
}

bool DebugGUI::initWindow() {
    if (window_initialized_) return true;

    GLFWwindow* main_window = glfwGetCurrentContext();

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* win = glfwCreateWindow(650, 550, "Performance Monitor - Moonmic", NULL, main_window);
    if (!win) {
        moonmic::logError() << "[DebugGUI] Failed to create debug window" << std::endl;
        glfwMakeContextCurrent(main_window);
        return false;
    }
    debug_window_ = win;

    glfwMakeContextCurrent(win);

    ImGuiContext* main_ctx = ImGui::GetCurrentContext();
    debug_imgui_context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(debug_imgui_context_));

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ImGui::SetCurrentContext(main_ctx);
    glfwMakeContextCurrent(main_window);

    window_initialized_ = true;
    moonmic::logInfo() << "[DebugGUI] Performance monitor window created (separate OS window)" << std::endl;
    return true;
}

void DebugGUI::destroyWindow() {
    if (debug_imgui_context_) {

        ImGuiContext* main_ctx = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(debug_imgui_context_));
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(static_cast<ImGuiContext*>(debug_imgui_context_));
        debug_imgui_context_ = nullptr;
        ImGui::SetCurrentContext(main_ctx);
    }

    if (debug_window_) {
        glfwDestroyWindow(static_cast<GLFWwindow*>(debug_window_));
        debug_window_ = nullptr;
    }
    window_initialized_ = false;
}

void DebugGUI::open() {
    if (!window_initialized_) {
        if (!initWindow()) return;
    }
    visible_ = true;
    if (debug_window_) {
        glfwShowWindow(static_cast<GLFWwindow*>(debug_window_));
        glfwFocusWindow(static_cast<GLFWwindow*>(debug_window_));
    }
    moonmic::logInfo() << "[DebugGUI] Performance monitor opened" << std::endl;
}

void DebugGUI::close() {
    visible_ = false;
    if (debug_window_) {
        glfwHideWindow(static_cast<GLFWwindow*>(debug_window_));
    }
}

void DebugGUI::toggle() {
    if (visible_) {
        close();
    } else {
        open();
    }
}

void DebugGUI::collectSystemMetrics() {
#ifdef _WIN32

    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        ULARGE_INTEGER now;
        now.LowPart = user.dwLowDateTime;
        now.HighPart = user.dwHighDateTime;

        FILETIME create, exit, kern, usr;
        GetProcessTimes(process_handle_, &create, &exit, &kern, &usr);
        ULARGE_INTEGER proc_now;
        proc_now.LowPart = usr.dwLowDateTime;
        proc_now.HighPart = usr.dwHighDateTime;

        uint64_t sys_delta = now.QuadPart - last_sys_time_.QuadPart;
        uint64_t proc_delta = proc_now.QuadPart - last_cpu_time_.QuadPart;

        if (sys_delta > 0) {
            current_cpu_ = (float)(proc_delta * 100.0 / sys_delta);
            current_cpu_ = std::clamp(current_cpu_, 0.0f, 100.0f);
        }

        last_sys_time_ = now;
        last_cpu_time_ = proc_now;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(process_handle_, &pmc, sizeof(pmc))) {
        current_memory_ = pmc.WorkingSetSize / (1024.0f * 1024.0f);
    }
#else

    current_cpu_ = 0.0f;
    current_memory_ = 0.0f;
#endif
}

void DebugGUI::update(float delta_time, const AudioStats& stats, bool connected, bool receiving) {

    auto now = std::chrono::steady_clock::now();
    uptime_seconds_ = std::chrono::duration<float>(now - start_time_).count();

    connection_led_.update(delta_time);
    data_led_.update(delta_time);
    data_flow_.update(delta_time, receiving);

    if (connected) {
        connection_led_.setState(AnimatedLED::State::CONNECTED);
    } else {
        connection_led_.setState(AnimatedLED::State::OFF);
    }

    if (receiving) {
        data_led_.setState(AnimatedLED::State::ACTIVE);
        data_led_.flash();
    } else if (connected) {
        data_led_.setState(AnimatedLED::State::CONNECTED);
    } else {
        data_led_.setState(AnimatedLED::State::OFF);
    }

    sample_timer_ += delta_time;
    if (sample_timer_ >= sample_interval_) {
        sample_timer_ = 0.0f;

        uint64_t packet_delta = stats.packets_received - last_packets_;
        uint64_t byte_delta = stats.bytes_received - last_bytes_;

        packets_per_second_ = packet_delta / sample_interval_;
        throughput_kbps_ = (byte_delta / 1024.0f) / sample_interval_;

        last_packets_ = stats.packets_received;
        last_bytes_ = stats.bytes_received;
        last_bytes_ = stats.bytes_received;
        total_packets_ = stats.packets_received;
        total_bytes_ = stats.bytes_received;
        dropped_packets_ = stats.packets_dropped;
        dropped_packets_lag_ = stats.packets_dropped_lag;
        current_rtt_ = stats.rtt_ms;

        collectSystemMetrics();

        PerformanceSample sample;
        sample.timestamp = uptime_seconds_;
        sample.packets_per_second = packets_per_second_;
        sample.throughput_kbps = throughput_kbps_;
        sample.cpu_usage_percent = current_cpu_;
        sample.memory_mb = current_memory_;
        sample.latency_ms = (current_rtt_ >= 0) ? (float)current_rtt_ : 0.0f;
        sample.jitter_ms = 0.0f;
        sample.packet_loss_percent =
            (total_packets_ > 0) ? (dropped_packets_ * 100.0f / (total_packets_ + dropped_packets_)) : 0.0f;

        history_.addSample(sample);

        packets_graph_->update(history_.getPacketsPerSecond(), packets_per_second_);
        throughput_graph_->update(history_.getThroughputKbps(), throughput_kbps_);
        cpu_graph_->update(history_.getCpuUsage(), current_cpu_);
        memory_graph_->update(history_.getMemoryMb(), current_memory_);
        latency_graph_->update(history_.getLatency(), sample.latency_ms);
    }
}

void DebugGUI::drawStatusIndicators(bool connected, bool receiving, bool paused) {
    ImGui::BeginGroup();

    if (connected) {
        connection_led_.setState(AnimatedLED::State::CONNECTED);
    } else {
        connection_led_.setState(AnimatedLED::State::OFF);
    }
    connection_led_.draw("Connection", 10.0f);

    if (paused) {

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float radius = 10.0f;

        ImU32 yellow = IM_COL32(255, 200, 0, 255);
        ImU32 yellow_glow = IM_COL32(255, 200, 0, 80);
        draw_list->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius + 2, yellow_glow);
        draw_list->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, yellow);

        ImGui::Dummy(ImVec2(radius * 2 + 4, radius * 2));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "PAUSED");
    } else {
        if (receiving) {
            data_led_.setState(AnimatedLED::State::ACTIVE);
        } else {
            data_led_.setState(AnimatedLED::State::OFF);
        }
        data_led_.draw("Data", 10.0f);
    }

    if (connected && receiving && !paused) {
        ImGui::Text("Stream:");
        ImGui::SameLine();
        data_flow_.draw(120.0f, 16.0f);
    }

    ImGui::EndGroup();
}

void DebugGUI::render() {
    if (!visible_ || !debug_window_ || !debug_imgui_context_) return;

    GLFWwindow* win = static_cast<GLFWwindow*>(debug_window_);

    if (glfwWindowShouldClose(win)) {
        close();
        glfwSetWindowShouldClose(win, GLFW_FALSE);
        return;
    }

    GLFWwindow* main_window = glfwGetCurrentContext();
    ImGuiContext* main_ctx = ImGui::GetCurrentContext();

    glfwMakeContextCurrent(win);
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(debug_imgui_context_));

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (ImGui::Begin("DebugContent", nullptr, window_flags)) {
        renderContent();
    }
    ImGui::End();

    ImGui::Render();

    int display_w, display_h;
    glfwGetFramebufferSize(win, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(win);

    ImGui::SetCurrentContext(main_ctx);
    glfwMakeContextCurrent(main_window);
}

void DebugGUI::renderContent() {

    ImGui::Text("History:");
    ImGui::SameLine();
    if (ImGui::RadioButton("10s", graph_interval_ == GraphInterval::SECONDS_10)) {
        graph_interval_ = GraphInterval::SECONDS_10;
        history_.setMaxSamples(100);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("30s", graph_interval_ == GraphInterval::SECONDS_30)) {
        graph_interval_ = GraphInterval::SECONDS_30;
        history_.setMaxSamples(300);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("60s", graph_interval_ == GraphInterval::SECONDS_60)) {
        graph_interval_ = GraphInterval::SECONDS_60;
        history_.setMaxSamples(600);
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 160);

    int hours = (int)uptime_seconds_ / 3600;
    int minutes = ((int)uptime_seconds_ % 3600) / 60;
    int seconds = (int)uptime_seconds_ % 60;
    ImGui::Text("Uptime: %02d:%02d:%02d", hours, minutes, seconds);

    ImGui::Separator();

    if (ImGui::BeginTabBar("DebugTabs")) {
        if (ImGui::BeginTabItem("Statistics")) {
            renderStatisticsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Performance")) {
            renderPerformanceTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Network")) {
            renderNetworkTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("System")) {
            renderSystemTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void DebugGUI::renderStatisticsTab() {
    ImGui::Columns(2, nullptr, false);

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Audio Stream");
    ImGui::Separator();
    ImGui::Text("Packets Received:");
    ImGui::SameLine(180);
    ImGui::Text("%llu", (unsigned long long)total_packets_);
    ImGui::Text("Packets Dropped:");
    ImGui::SameLine(180);
    ImGui::Text("%llu", (unsigned long long)dropped_packets_);

    if (dropped_packets_lag_ > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Auto-Corrected:");
        ImGui::SameLine(180);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%llu (Lag)", (unsigned long long)dropped_packets_lag_);
    }

    ImGui::Text("Bytes Received:");
    ImGui::SameLine(180);
    ImGui::Text("%.2f MB", total_bytes_ / (1024.0f * 1024.0f));
    ImGui::Text("Current Rate:");
    ImGui::SameLine(180);
    ImGui::Text("%.1f pps", packets_per_second_);
    ImGui::Text("Throughput:");
    ImGui::SameLine(180);
    ImGui::Text("%.2f KB/s", throughput_kbps_);

    ImGui::NextColumn();

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Quality");
    ImGui::Separator();

    float loss_percent =
        (total_packets_ > 0) ? (dropped_packets_ * 100.0f / (total_packets_ + dropped_packets_)) : 0.0f;

    ImVec4 loss_color = (loss_percent < 1.0f)   ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f)
                        : (loss_percent < 5.0f) ? ImVec4(1.0f, 0.8f, 0.0f, 1.0f)
                                                : ImVec4(1.0f, 0.2f, 0.2f, 1.0f);

    ImGui::Text("Packet Loss:");
    ImGui::SameLine(120);
    ImGui::TextColored(loss_color, "%.2f%%", loss_percent);

    ImGui::Text("Stream Quality:");
    float quality = 100.0f - loss_percent;
    ImGui::ProgressBar(quality / 100.0f, ImVec2(-1, 0), "");

    ImGui::Columns(1);
}

void DebugGUI::renderPerformanceTab() {
    ImVec2 graph_size(ImGui::GetContentRegionAvail().x, 80);

    packets_graph_->draw(graph_size);
    ImGui::Spacing();

    throughput_graph_->draw(graph_size);
    ImGui::Spacing();

    cpu_graph_->draw(graph_size);
    ImGui::Spacing();

    memory_graph_->draw(graph_size);
}

void DebugGUI::renderNetworkTab() {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Network Statistics");
    ImGui::Separator();

    if (current_rtt_ >= 0) {
        ImGui::Text("Round-Trip Time (RTT):");
        ImGui::SameLine(180);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%d ms", current_rtt_);
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting for RTT...");
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImVec2 graph_size(ImGui::GetContentRegionAvail().x, 100);
    latency_graph_->draw(graph_size);
}

void DebugGUI::renderSystemTab() {
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1.0f), "System Resources");
    ImGui::Separator();

    ImGui::Text("CPU Usage:");
    ImGui::SameLine(120);
    ImGui::ProgressBar(current_cpu_ / 100.0f, ImVec2(200, 0));
    ImGui::SameLine();
    ImGui::Text("%.1f%%", current_cpu_);

    ImGui::Text("Memory:");
    ImGui::SameLine(120);
    ImGui::ProgressBar(current_memory_ / 500.0f, ImVec2(200, 0));
    ImGui::SameLine();
    ImGui::Text("%.1f MB", current_memory_);

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Build Information");
    ImGui::Text("Platform:");
    ImGui::SameLine(120);
#ifdef _WIN32
    ImGui::Text("Windows x64");
#else
    ImGui::Text("Linux x64");
#endif

    ImGui::Text("Build Type:");
    ImGui::SameLine(120);
#ifdef NDEBUG
    ImGui::Text("Release");
#else
    ImGui::Text("Debug");
#endif
}

} // namespace moonmic
