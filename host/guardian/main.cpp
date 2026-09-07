
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <iomanip>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "../src/guardian_state.h"
#include "../src/config.h"
#include "../src/logger.h"
#include "../src/guardian_launcher.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include "../src/platform/windows/audio_device_manager.h"
#else
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif

using namespace moonmic;

namespace platform {
bool isProcessAlive(unsigned long pid) {
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return false;
    DWORD exitCode;
    if (GetExitCodeProcess(hProcess, &exitCode)) {
        CloseHandle(hProcess);
        return exitCode == STILL_ACTIVE;
    }
    CloseHandle(hProcess);
    return false;
#else
    return (kill(pid, 0) == 0);
#endif
}

bool restoreMicrophone(const std::string& micId) {
#ifdef _WIN32
    AudioDeviceManager devMgr;
    return devMgr.setDefaultRecordingDevice(micId);
#else
    std::cout << "[Guardian] Restoration not yet implemented on Linux" << std::endl;
    return false;
#endif
}

std::string getCurrentMicName() {
#ifdef _WIN32
    AudioDeviceManager devMgr;
    return devMgr.getCurrentDefaultRecordingDevice().name;
#else
    return "Unknown (Linux)";
#endif
}
} // namespace platform

enum class GuardianMode { MONITORING, TEST_MODE, CRASH_DETECTED, RESULT_SUCCESS, RESULT_FAILED };

struct UIState {
    GuardianMode mode = GuardianMode::MONITORING;
    GuardianState state;
    std::string message;
    bool shouldExit = false;
    bool windowVisible = false;
};

std::string getLogPath() {
    return Logger::getLogPath();
}

void dumpLog(UIState& ui) {
    std::string logPath = getLogPath();
    if (!std::filesystem::exists(logPath)) {
        ui.message = "Log file not found: " + logPath;
        ui.mode = GuardianMode::RESULT_FAILED;
        return;
    }

    auto now = std::time(nullptr);
    std::tm localTime;
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "moonmic_crash_%Y%m%d_%H%M%S.log", &localTime);
    std::string dumpName = buf;

    std::filesystem::path p(logPath);
    std::string dumpPath = (p.parent_path() / dumpName).string();

    try {
        std::filesystem::copy_file(logPath, dumpPath, std::filesystem::copy_options::overwrite_existing);
        ui.message = "Log dumped to:\n" + dumpPath;
        ui.mode = GuardianMode::RESULT_SUCCESS;
    } catch (const std::exception& e) {
        ui.message = "Failed to dump log: " + std::string(e.what());
        ui.mode = GuardianMode::RESULT_FAILED;
    }
}

void viewLog() {
    std::string logPath = getLogPath();
#ifdef _WIN32

    std::string params = "/K echo [Moonmic Log Viewer] && echo File: " + logPath +
                         " && echo ---------------------------------------- && type \"" + logPath + "\"";
    ShellExecuteA(NULL, "open", "cmd.exe", params.c_str(), NULL, SW_SHOW);
#else
    std::string cmd = "xdg-open \"" + logPath + "\"";
    system(cmd.c_str());
#endif
}

void renderUI(UIState& ui) {
    if (!ui.windowVisible) return;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("Guardian", nullptr, flags);

    switch (ui.mode) {
    case GuardianMode::TEST_MODE:
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "Moonmic Guardian - Test Mode");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("This is the watchdog process. It normally runs in the background.");
        ImGui::TextWrapped("Click 'Test Restore' to verify if your microphone can be restored to its original state.");
        ImGui::Spacing();
        if (ImGui::Button("Test Restore", ImVec2(120, 40))) {
            if (GuardianStateManager::readState(ui.state)) {
                if (platform::restoreMicrophone(ui.state.original_mic_id)) {
                    ui.mode = GuardianMode::RESULT_SUCCESS;
                    ui.message = "Microphone restored to: " + ui.state.original_mic_name;
                    GuardianStateManager::deleteState();
                } else {
                    ui.mode = GuardianMode::RESULT_FAILED;
                    ui.message = "Failed to restore microphone.";
                }
            } else {
                ui.mode = GuardianMode::RESULT_FAILED;
                ui.message = "No saved microphone state found. Run moonmic-host first.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Exit", ImVec2(120, 40))) {
            ui.shouldExit = true;
        }
        break;

    case GuardianMode::CRASH_DETECTED:
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Moonmic Host Crashed!");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("The main application closed unexpectedly.");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ui.message.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Dump Log", ImVec2(140, 30))) {
            dumpLog(ui);
        }
        ImGui::SameLine();
        if (ImGui::Button("View Log", ImVec2(140, 30))) {
            viewLog();
        }

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 40))) {
            ui.shouldExit = true;
        }
        break;

    case GuardianMode::RESULT_SUCCESS:
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Success");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ui.message.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 40))) {
            ui.shouldExit = true;
        }
        break;

    case GuardianMode::RESULT_FAILED:
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Restoration Failed");
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", ui.message.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 40))) {
            ui.shouldExit = true;
        }
        break;

    default:
        ImGui::Text("Monitoring...");
        break;
    }

    ImGui::End();
}

int main(int argc, char* argv[]) {
    UIState ui;
    unsigned long targetPid = 0;

    if (argc >= 2) {
        try {
            targetPid = std::stoul(argv[1]);
            ui.mode = GuardianMode::MONITORING;
            ui.windowVisible = false;
        } catch (...) {
            ui.mode = GuardianMode::TEST_MODE;
            ui.windowVisible = true;
        }
    } else {
        ui.mode = GuardianMode::TEST_MODE;
        ui.windowVisible = true;
    }

    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    if (!ui.windowVisible) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    GLFWwindow* window = glfwCreateWindow(400, 300, "Moonmic Guardian", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    ImGui::StyleColorsDark();

    std::cout << "[Guardian] Watchdog active. Using Local Events for sync." << std::endl;

#ifdef _WIN32
    HANDLE hShutdownEvent = OpenEventA(SYNCHRONIZE, FALSE, GuardianLauncher::SHUTDOWN_EVENT_NAME);
    HANDLE hRestartEvent = OpenEventA(SYNCHRONIZE, FALSE, GuardianLauncher::RESTART_EVENT_NAME);

    if (!hShutdownEvent) {
        hShutdownEvent = CreateEventA(NULL, TRUE, FALSE, GuardianLauncher::SHUTDOWN_EVENT_NAME);
    }
    if (!hRestartEvent) {
        hRestartEvent = CreateEventA(NULL, TRUE, FALSE, GuardianLauncher::RESTART_EVENT_NAME);
    }
#endif

    while (!glfwWindowShouldClose(window) && !ui.shouldExit) {

        if (ui.mode == GuardianMode::MONITORING) {
            if (!platform::isProcessAlive(targetPid)) {

#ifdef _WIN32
                bool restartRequested = false;
                bool normalShutdown = false;

                if (hRestartEvent) {
                    if (WaitForSingleObject(hRestartEvent, 0) == WAIT_OBJECT_0) {
                        restartRequested = true;
                    }
                }

                if (hShutdownEvent) {
                    if (WaitForSingleObject(hShutdownEvent, 0) == WAIT_OBJECT_0) {
                        normalShutdown = true;
                    }
                }

                if (restartRequested) {

                    std::cout << "[Guardian] Restart requested. Relaunching host..." << std::endl;

                    char exePath[MAX_PATH];
                    GetModuleFileNameA(NULL, exePath, MAX_PATH);
                    std::filesystem::path hostPath = std::filesystem::path(exePath).parent_path() / "moonmic-host.exe";

                    ShellExecuteA(NULL, "open", hostPath.string().c_str(), NULL, NULL, SW_SHOW);
                    ui.shouldExit = true;

                } else if (normalShutdown) {

                    if (GuardianStateManager::readState(ui.state)) {

                        platform::restoreMicrophone(ui.state.original_mic_id);
                        GuardianStateManager::deleteState();
                    }
                    ui.shouldExit = true;
                } else {

                    if (GuardianStateManager::readState(ui.state)) {
                        ui.mode = GuardianMode::CRASH_DETECTED;

                        if (platform::restoreMicrophone(ui.state.original_mic_id)) {
                            ui.message = "Microphone restored automatically.";
                            GuardianStateManager::deleteState();
                        } else {
                            ui.message = "Automatic restoration failed.";
                        }

                        ui.windowVisible = true;
                        glfwShowWindow(window);
                    } else {

                        ui.shouldExit = true;
                    }
                }
#else

                if (GuardianStateManager::readState(ui.state)) {
                    ui.mode = GuardianMode::CRASH_DETECTED;
                    ui.windowVisible = true;
                    glfwShowWindow(window);
                } else {
                    ui.shouldExit = true;
                }
#endif
            }
        }

        if (ui.windowVisible) {
            glfwPollEvents();
        } else {

            glfwWaitEventsTimeout(0.5);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        renderUI(ui);

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

#ifdef _WIN32
    if (hShutdownEvent) {
        CloseHandle(hShutdownEvent);
    }
    if (hRestartEvent) {
        CloseHandle(hRestartEvent);
    }
#endif

    return 0;
}
