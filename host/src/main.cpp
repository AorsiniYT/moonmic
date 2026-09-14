
#include "config.h"
#include "logger.h"
#include "audio_receiver.h"
#include "sunshine_integration.h"
#include "display_manager.h"
#include "sunshine_webui.h"
#include "single_instance.h"
#include "version_checker.h"
#include "version.h"
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <filesystem>
#include <future>

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <shellapi.h>

#include "platform/windows/driver_installer.h"
#include "platform/windows/audio_utils.h"
#include "platform/windows/audio_device_manager.h"
#include "guardian_launcher.h"
#endif

#ifdef USE_IMGUI
#include "debug_gui.h"
#include "display_settings_gui.h"
#include "gui_helper.h"
#include "sunshine_settings_gui.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#endif

using namespace moonmic;

static bool g_running = true;
static AudioReceiver* g_receiver = nullptr;

bool g_debug_mode = false;

static bool g_restart_requested = false;

#ifdef USE_IMGUI

static bool g_update_available = false;
static bool g_update_check_done = false;
static bool g_update_dismissed = false;
static std::string g_latest_version;
static std::string g_download_url;

static void setWindowIcon(GLFWwindow* window) {
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    int channels = 0;

#ifdef _WIN32
    HRSRC resource = FindResourceA(nullptr, "IDR_WINDOW_ICON_PNG", RT_RCDATA);
    if (resource) {
        HGLOBAL loaded = LoadResource(nullptr, resource);
        const void* data = loaded ? LockResource(loaded) : nullptr;
        const DWORD size = loaded ? SizeofResource(nullptr, resource) : 0;
        if (data && size > 0) {
            pixels =
                stbi_load_from_memory(static_cast<const unsigned char*>(data), size, &width, &height, &channels, 4);
        }
    }
#elif defined(__linux__)
    char executable[PATH_MAX + 1];
    const ssize_t length = readlink("/proc/self/exe", executable, PATH_MAX);
    if (length > 0) {
        executable[length] = '\0';
        const std::filesystem::path path = std::filesystem::path(executable).parent_path() / "moonmic.png";
        pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    }
#ifdef MOONMIC_INSTALLED_ICON
    if (!pixels) {
        pixels = stbi_load(MOONMIC_INSTALLED_ICON, &width, &height, &channels, 4);
    }
#endif
#endif

    if (!pixels) {
        return;
    }

    GLFWimage icon = {width, height, pixels};
    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(pixels);
}
#endif

void signal_handler(int) {
    std::cout << "\n[Main] Shutting down..." << std::endl;
    g_running = false;

    if (g_receiver) {
        g_receiver->stop();
    }
}

#ifdef USE_IMGUI

void renderGUI(GLFWwindow* window, AudioReceiver& receiver, SunshineIntegration& sunshine,
               SunshineWebUI& sunshine_webui, DisplayManager& display_mgr, DisplaySettingsGUI& display_settings_gui,
               SunshineSettingsGUI& sunshine_settings_gui, DebugGUI& debug_gui, Config& config) {

    static std::future<bool> uninstall_future;
    static bool is_uninstalling = false;
    static std::future<bool> install_future;
    static bool is_installing = false;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags window_flags = 0;
    window_flags |= ImGuiWindowFlags_NoTitleBar;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    window_flags |= ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("Moonmic Host", nullptr, window_flags);

    ImGui::Text("Moonmic host by AorsiniYT - v%s", MOONMIC_VERSION);
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::SmallButton("About")) {
        ImGui::OpenPopup("About Moonmic");
    }
    ImGui::Separator();

#ifdef _WIN32

    ImGui::Text("Virtual Audio Driver");

    static bool show_driver_manager = false;
    DriverInstaller installer;

    static int selected_driver = 0;
    const char* driver_names[] = {"VB-CABLE", "Steam Streaming Microphone (WDM-KS)"};

    if (is_installing && install_future.valid()) {
        auto status = install_future.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            bool result = install_future.get();
            is_installing = false;

            ImGui::CloseCurrentPopup();

            if (result) {

                selected_driver = 1;
                config.audio.driver_device_name = "Steam Streaming Microphone";
                config.audio.recording_endpoint_name = "Steam Streaming Microphone";

                std::string config_path = Config::getDefaultConfigPath();
                config.save(config_path);

                std::cout << "[Main] Driver installed. Prompting for restart..." << std::endl;

                ImGui::OpenPopup("Install Success");
            } else {
                ImGui::OpenPopup("Install Failed");
            }
        }
    }

    if (is_uninstalling && uninstall_future.valid()) {
        auto status = uninstall_future.wait_for(std::chrono::milliseconds(0));
        if (status == std::future_status::ready) {
            bool result = uninstall_future.get();
            is_uninstalling = false;

            ImGui::CloseCurrentPopup();

            if (result) {
                ImGui::OpenPopup("Uninstall Success");
            } else {
                ImGui::OpenPopup("Uninstall Failed");
            }
        }
    }

    if (config.audio.driver_device_name.find("VB-Audio") != std::string::npos) {
        selected_driver = 0;
    } else if (config.audio.driver_device_name.find("Steam") != std::string::npos ||
               config.audio.recording_endpoint_name.find("Steam") != std::string::npos) {
        selected_driver = 1;
    }

    ImGui::Text("Driver Type:");
    ImGui::SameLine();
    if (ImGui::Combo("##DriverType", &selected_driver, driver_names, IM_ARRAYSIZE(driver_names))) {

        if (selected_driver == 0) {
            config.audio.driver_device_name = "VB-Audio Virtual Cable";
            config.audio.recording_endpoint_name = "CABLE Output";
        } else {

            config.audio.driver_device_name = "Steam Streaming Microphone";
            config.audio.recording_endpoint_name = "Steam Streaming Microphone";
        }

        std::string config_path = Config::getDefaultConfigPath();
        if (config.save(config_path)) {
            std::cout << "[Config] Auto-saved driver selection: " << config.audio.driver_device_name << std::endl;
        }

        moonmic::platform::windows::ChangeDeviceState(config.audio.driver_device_name, true);

        if (receiver.isRunning()) {
            std::cout << "[Main] Switching to " << driver_names[selected_driver] << ", restarting receiver..."
                      << std::endl;
            receiver.stop();

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            std::cout << "[Main] Driver switched, attempting to start receiver..." << std::endl;
        }

        if (receiver.start(config)) {
            std::cout << "[Main] Receiver restarted successfully with new driver" << std::endl;
        } else {
            std::cerr << "[Main] Failed to restart receiver with new driver" << std::endl;
        }
    }

    bool driver_installed = false;
    if (selected_driver == 0) {
        driver_installed = installer.isVBCableInstalled();
    } else {
        driver_installed = installer.isSteamMicrophoneInstalled();
    }

    if (driver_installed) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "[OK] %s Installed", driver_names[selected_driver]);

        if (selected_driver == 0) {
            std::string input_device = installer.getVBCableInputDevice();
            std::string output_device = installer.getVBCableOutputDevice();
            if (!input_device.empty()) {
                ImGui::Text("  Input: %s", input_device.c_str());
            }
            if (!output_device.empty()) {
                ImGui::Text("  Output: %s", output_device.c_str());
            }
        }
    } else {
        ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "[!] %s Not Installed", driver_names[selected_driver]);
        ImGui::TextWrapped("Virtual audio driver is required for audio routing.");
    }

    ImGui::Spacing();

    if (ImGui::Button("Driver Manager", ImVec2(150, 30))) {
        show_driver_manager = true;
    }

    ImGui::SameLine();

    if (!driver_installed) {
        if (ImGui::Button("Quick Install", ImVec2(150, 30))) {
            if (!DriverInstaller::isRunningAsAdmin()) {
                ImGui::OpenPopup("Need Admin");
            } else {
                if (selected_driver == 0) {

                    if (installer.installVBCable()) {
                        ImGui::OpenPopup("Install Success");
                    } else {
                        ImGui::OpenPopup("Install Failed");
                    }
                } else {

                    is_installing = true;
                    ImGui::OpenPopup("Installing Driver");
                    install_future =
                        std::async(std::launch::async, [&installer]() { return installer.installSteamMicrophone(); });
                }
            }
        }
    }

    if (show_driver_manager) {
        ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Driver Manager", &show_driver_manager)) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Virtual Audio Driver Configuration");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Steam Streaming Microphone (Recommended)");
            bool steam_installed = installer.isSteamMicrophoneInstalled();

            if (steam_installed) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Installed");
                ImGui::TextWrapped("Ready for low-latency WDM-KS audio.");

                ImGui::Spacing();
                if (ImGui::Button("Uninstall Steam Driver", ImVec2(200, 25))) {
                    if (!DriverInstaller::isRunningAsAdmin()) {
                        ImGui::OpenPopup("Need Admin");
                    } else {

                        receiver.stop();
                        std::cout << "[Main] Stopped audio receiver for driver uninstall" << std::endl;

                        is_uninstalling = true;
                        ImGui::OpenPopup("Uninstalling Driver");

                        uninstall_future = std::async(std::launch::async,
                                                      [&installer]() { return installer.uninstallSteamMicrophone(); });
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Status: Not Installed");
                if (ImGui::Button("Install Steam Driver", ImVec2(200, 25))) {
                    if (!DriverInstaller::isRunningAsAdmin()) {
                        ImGui::OpenPopup("Need Admin");
                    } else {
                        is_installing = true;
                        ImGui::OpenPopup("Installing Driver");
                        install_future = std::async(std::launch::async,
                                                    [&installer]() { return installer.installSteamMicrophone(); });
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "VB-CABLE Driver (Alternative)");
            bool vb_installed = installer.isVBCableInstalled();

            if (vb_installed) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Installed");
                std::string input = installer.getVBCableInputDevice();
                std::string output = installer.getVBCableOutputDevice();
                if (!input.empty()) ImGui::Text("  Input: %s", input.c_str());
                if (!output.empty()) ImGui::Text("  Output: %s", output.c_str());

                ImGui::Spacing();
                if (ImGui::Button("Uninstall VB-CABLE", ImVec2(200, 25))) {
                    if (!DriverInstaller::isRunningAsAdmin()) {
                        ImGui::OpenPopup("Need Admin");
                    } else {

                        receiver.stop();
                        std::cout << "[Main] Stopped audio receiver for driver uninstall" << std::endl;

                        if (installer.uninstallVBCable()) {
                            ImGui::OpenPopup("Uninstall Started");
                        } else {
                            ImGui::OpenPopup("Uninstall Failed");
                        }
                    }
                }
            } else {
                ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "Status: Not Installed");
                if (ImGui::Button("Install VB-CABLE", ImVec2(200, 25))) {
                    if (!DriverInstaller::isRunningAsAdmin()) {
                        ImGui::OpenPopup("Need Admin");
                    } else {
                        if (installer.installVBCable()) {
                            ImGui::OpenPopup("Install Success");
                        } else {
                            ImGui::OpenPopup("Install Failed");
                        }
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::TextWrapped("Note: Administrator privileges are required for driver operations.");

            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(100, 25))) {
                show_driver_manager = false;
            }
        }
        ImGui::End();
    }

    static bool initial_setup_checked = false;
    if (!initial_setup_checked) {
        if (!installer.isAnyDriverInstalled()) {
            ImGui::OpenPopup("Initial Setup Wizard");
        }
        initial_setup_checked = true;
    }

    if (ImGui::BeginPopupModal("Initial Setup Wizard", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "Welcome to Moonmic!");
        ImGui::Separator();
        ImGui::Text("No virtual audio driver was detected on your system.");
        ImGui::Text("You need at least one driver to route audio correctly.");
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Recommended: Steam Streaming Microphone");
        ImGui::TextWrapped("Provides lower latency and better stability via WDM-KS.");

        ImGui::Spacing();

        if (ImGui::Button("Install Steam Driver (Recommended)", ImVec2(300, 40))) {
            if (!DriverInstaller::isRunningAsAdmin()) {
                ImGui::OpenPopup("Need Admin");
            } else {
                ImGui::CloseCurrentPopup();
                is_installing = true;
                ImGui::OpenPopup("Installing Driver");
                install_future =
                    std::async(std::launch::async, [&installer]() { return installer.installSteamMicrophone(); });
            }
        }

        ImGui::Spacing();
        ImGui::Text("Alternative: VB-CABLE");

        if (ImGui::Button("Install VB-CABLE", ImVec2(300, 30))) {
            if (!DriverInstaller::isRunningAsAdmin()) {
                ImGui::OpenPopup("Need Admin");
            } else {
                if (installer.installVBCable()) {
                    ImGui::CloseCurrentPopup();
                    ImGui::OpenPopup("Install Success");
                } else {
                    ImGui::OpenPopup("Install Failed");
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();

        if (ImGui::Button("Skip for now", ImVec2(120, 30))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Need Admin", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Administrator privileges required.");
        ImGui::Text("The application will restart with admin rights.");
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (DriverInstaller::restartAsAdmin()) {

                std::cout << "[Main] Restarting as admin, closing current instance..." << std::endl;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Install Success", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Driver installed successfully!");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("The application needs to restart to initialize the new driver.");
        ImGui::TextWrapped("This will happen automatically via the Guardian process.");
        ImGui::Spacing();

        if (ImGui::Button("Restart Application", ImVec2(180, 40))) {
            std::cout << "[Main] User requested restart. Signaling Guardian..." << std::endl;
            moonmic::GuardianLauncher::signalRestart();
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Install Failed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Failed to install driver.");
        ImGui::Text("Please check the console for error details.");
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Installing Driver", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Installing Steam Streaming Microphone...");
        ImGui::Text("Please wait, this may take a few moments.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Working... %c", "|/-\\"[(int)(ImGui::GetTime() / 0.05f) & 3]);

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Uninstalling Driver", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Uninstalling Steam Streaming Microphone...");
        ImGui::Text("Please wait, this may take a few moments.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Working... %c", "|/-\\"[(int)(ImGui::GetTime() / 0.05f) & 3]);

        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Uninstall Success", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Driver uninstalled successfully!");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("The driver has been removed from your system.");
        ImGui::Spacing();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Uninstall Failed", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Failed to uninstall driver.");
        ImGui::Text("Please check the console for error details.");
        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
#endif

    ImGui::Text("Sunshine Integration");

    if (sunshine.isPaired()) {

        ImGui::TextColored(ImVec4(0, 1, 0, 1), "[OK] Sunshine Paired");

        if (sunshine_webui.isLoggedIn()) {
            ImGui::Text("Web UI: Logged in as %s", config.sunshine.webui_username.c_str());

            if (ImGui::Button("Sunshine Settings")) {
                sunshine_settings_gui.open();
            }
            ShowHelpTooltip(Tooltips::SUNSHINE_WEBUI);
        } else {

            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Web UI: Not logged in");
            ImGui::TextWrapped("Login required for client validation & security");

            if (ImGui::Button("Login to Sunshine Web UI")) {
                ImGui::OpenPopup("Sunshine Web UI Login");
            }
            ShowHelpTooltip(Tooltips::SUNSHINE_WEBUI);

            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

            if (ImGui::BeginPopupModal("Sunshine Web UI Login", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Login to Sunshine Web UI");
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                static char webui_username[128] = "";
                static char webui_password[128] = "";
                static std::string login_error = "";

                ImGui::InputText("Username", webui_username, sizeof(webui_username));
                ImGui::InputText("Password", webui_password, sizeof(webui_password), ImGuiInputTextFlags_Password);

                if (!login_error.empty()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", login_error.c_str());
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGui::Button("Login", ImVec2(120, 0))) {
                    if (sunshine_webui.login(webui_username, webui_password)) {
                        login_error = "";
                        memset(webui_password, 0, sizeof(webui_password));
                        ImGui::CloseCurrentPopup();
                    } else {
                        login_error = "Invalid Sunshine credentials";
                    }
                }

                ImGui::SameLine();

                if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                    login_error = "";
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }
    } else {

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "[!] Sunshine Web UI Login Required");
        ImGui::TextWrapped("Please login to Sunshine Web UI to access full functionality.");
        ImGui::Spacing();
        if (ImGui::Button("Login to Sunshine Web UI")) {
            ImGui::OpenPopup("Sunshine Web UI Login");
        }
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Sunshine Web UI Login", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Login to Sunshine Web UI");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        static char webui_username[128] = "";
        static char webui_password[128] = "";
        static std::string login_error = "";

        ImGui::InputText("Username", webui_username, sizeof(webui_username));
        ImGui::InputText("Password", webui_password, sizeof(webui_password), ImGuiInputTextFlags_Password);

        if (!login_error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "%s", login_error.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Login", ImVec2(120, 0))) {
            if (sunshine_webui.login(webui_username, webui_password)) {
                login_error = "";
                memset(webui_password, 0, sizeof(webui_password));
                ImGui::CloseCurrentPopup();
            } else {
                login_error = "Invalid Sunshine credentials";
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            login_error = "";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::Separator();

    auto stats = receiver.getStats();
    bool connected = stats.is_connected;
    bool receiving = stats.is_receiving;
    bool paused = stats.is_paused;

    ImGui::Text("Status");
    debug_gui.drawStatusIndicators(connected, receiving, paused);

    if (connected) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 200);
        if (!stats.client_name.empty()) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "%s", stats.client_name.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", stats.last_sender_ip.c_str());
        }
    }

    ImGui::Separator();

    ImGui::Text("Configuration");

    bool config_changed = false;

    int prev_port = config.server.port;
    if (ImGui::InputInt("Port", &config.server.port, 0, 0)) {
        if (config.server.port < 1 || config.server.port > 65535) {
            config.server.port = prev_port;
        } else if (config.server.port != prev_port) {
            config_changed = true;
        }
    }
    ShowHelpTooltip(Tooltips::PORT_CONFIG);

    int prev_channels = config.audio.channels;
    if (ImGui::InputInt("Channels", &config.audio.channels, 0, 0)) {
        if (config.audio.channels < 1 || config.audio.channels > 2) {
            config.audio.channels = prev_channels;
        } else if (config.audio.channels != prev_channels) {
            config_changed = true;
        }
    }
    ShowHelpTooltip(Tooltips::CHANNELS_CONFIG);

    if (config_changed) {
        std::string config_path = Config::getDefaultConfigPath();
        if (config.save(config_path)) {
            std::cout << "[Config] Auto-saved configuration changes" << std::endl;
        }
    }

    bool prev_whitelist = config.security.enable_whitelist;
    if (ImGui::Checkbox("Whitelist Enabled", &config.security.enable_whitelist)) {
        if (prev_whitelist != config.security.enable_whitelist) {

            std::string config_path = Config::getDefaultConfigPath();
            if (config.save(config_path)) {
                std::cout << "[Config] Auto-saved changes" << std::endl;
            }
        }
    }
    ShowHelpTooltip(Tooltips::WHITELIST);

    bool prev_speaker = config.audio.use_speaker_mode;
    if (ImGui::Checkbox("Speaker Mode (Debug)", &config.audio.use_speaker_mode)) {
        if (prev_speaker != config.audio.use_speaker_mode) {

            if (receiver.isRunning()) {
                receiver.switchAudioOutput(config.audio.use_speaker_mode);
            }

            std::string config_path = Config::getDefaultConfigPath();
            if (config.save(config_path)) {
                std::cout << "[Config] Auto-saved speaker mode: "
                          << (config.audio.use_speaker_mode ? "ON (direct playback)" : "OFF (VB-Cable)") << std::endl;
            }
        }
    }
    ShowHelpTooltip(Tooltips::SPEAKER_MODE);
    if (config.audio.use_speaker_mode) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Warning: Audio goes to speakers, NOT VB-Cable");
    } else {
        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Audio sent to VB-Cable (normal mode)");
    }

#ifdef _WIN32
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Microphone Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        static AudioDeviceManager deviceManager;
        static std::vector<AudioDeviceInfo> availableMics;
        static int selectedMicIndex = -1;
        static bool micsLoaded = false;

        if (!micsLoaded) {
            availableMics = deviceManager.enumerateRecordingDevices();
            micsLoaded = true;

            if (!config.audio.original_mic_id.empty()) {
                for (size_t i = 0; i < availableMics.size(); i++) {
                    if (availableMics[i].id == config.audio.original_mic_id) {
                        selectedMicIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
        }

        ImGui::Text("Current Default Microphone:");
        ShowHelpTooltip(Tooltips::CURRENT_DEFAULT_MIC);

        AudioDeviceInfo currentDefault = deviceManager.getCurrentDefaultRecordingDevice();
        if (!currentDefault.id.empty()) {
            ImVec4 color = currentDefault.is_virtual ? ImVec4(1, 0.5f, 0, 1) : ImVec4(0, 1, 0, 1);
            ImGui::TextColored(color, "  %s", currentDefault.name.c_str());
            if (currentDefault.is_virtual) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "(Virtual)");
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "  [No default microphone]");
        }

        ImGui::Spacing();

        ImGui::Text("Microphone to Restore on Exit:");
        ShowHelpTooltip(Tooltips::ORIGINAL_MIC_SELECTOR);

        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            availableMics = deviceManager.enumerateRecordingDevices();

            selectedMicIndex = -1;
            if (!config.audio.original_mic_id.empty()) {
                for (size_t i = 0; i < availableMics.size(); i++) {
                    if (availableMics[i].id == config.audio.original_mic_id) {
                        selectedMicIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
        }

        if (availableMics.empty()) {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "No microphones detected");
        } else {
            std::string preview = selectedMicIndex >= 0 && static_cast<size_t>(selectedMicIndex) < availableMics.size()
                                      ? availableMics[selectedMicIndex].name
                                      : "[Not Set]";

            if (ImGui::BeginCombo("##OriginalMic", preview.c_str())) {
                for (size_t i = 0; i < availableMics.size(); i++) {

                    if (availableMics[i].is_virtual) continue;

                    bool is_selected = (selectedMicIndex == static_cast<int>(i));
                    if (ImGui::Selectable(availableMics[i].name.c_str(), is_selected)) {
                        selectedMicIndex = static_cast<int>(i);
                        config.audio.original_mic_id = availableMics[i].id;

                        std::string config_path = Config::getDefaultConfigPath();
                        if (config.save(config_path)) {
                            std::cout << "[Config] Original microphone set to: " << availableMics[i].name << std::endl;
                        }
                    }

                    if (is_selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (selectedMicIndex >= 0 && static_cast<size_t>(selectedMicIndex) < availableMics.size()) {
                ImGui::TextColored(ImVec4(0, 1, 0, 1), "This microphone will be restored when the app closes");
            } else {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "No restore microphone is set");
            }
        }

        ImGui::Spacing();

        bool guardianActive = GuardianLauncher::isGuardianRunning();
        ImGui::Text("Guardian Watchdog:");
        ShowHelpTooltip(Tooltips::GUARDIAN_STATUS);
        ImGui::SameLine();
        if (guardianActive) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Active");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "(Monitoring for crashes)");
        } else {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Inactive");
        }
    }
#endif

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Checkbox("Debug Mode (Verbose Logs)", &g_debug_mode)) {
        std::cout << "[Main] Debug mode: " << (g_debug_mode ? "ON" : "OFF") << std::endl;

        DebugGUI::showConsole(g_debug_mode);
    }
    ShowHelpTooltip(Tooltips::DEBUG_MODE);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Shows console & detailed logs");

    if (g_debug_mode) {
        if (ImGui::Button("Performance Monitor")) {
            debug_gui.toggle();
        }
        if (debug_gui.isVisible()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1), "[OPEN]");
        }
    }

    ImGui::Separator();

    if (receiver.isRunning()) {

        if (receiver.isPaused()) {
            if (ImGui::Button("Resume", ImVec2(120, 30))) {
                receiver.resume();
            }
            ShowHelpTooltip(Tooltips::PAUSE_RESUME);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Paused");
        } else {
            if (ImGui::Button("Pause", ImVec2(120, 30))) {
                receiver.pause();
            }
            ShowHelpTooltip(Tooltips::PAUSE_RESUME);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Active");
        }

        ImGui::SameLine();

        if (ImGui::Button("Reload Sunshine")) {
            sunshine.reload();

            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Paired with Sunshine");

            std::string config_path = Config::getDefaultConfigPath();
            if (config.save(config_path)) {
                std::cout << "[Config] Auto-saved Sunshine client list" << std::endl;
            }
        }
        ShowHelpTooltip(Tooltips::RELOAD_SUNSHINE);

        ImGui::SameLine();

        if (ImGui::Button("Display Settings")) {
            display_settings_gui.open();
        }
        ShowHelpTooltip(Tooltips::DISPLAY_SETTINGS);
    } else {

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Receiver is not running");
        ImGui::Spacing();

#ifdef _WIN32
        if (driver_installed) {
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Driver detected but audio skipped.");
            ImGui::TextWrapped("The application needs to restart to initialize the audio engine with the new driver.");

            if (ImGui::Button("Restart Application", ImVec2(200, 30))) {
                std::cout << "[Main] User requested restart. Signaling Guardian..." << std::endl;
                g_restart_requested = true;
                moonmic::GuardianLauncher::signalRestart();
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
        } else {
            ImGui::TextWrapped("Please install the Virtual Audio Driver first to start.");
        }
#else
        ImGui::TextWrapped("Check your audio device configuration.");
        ImGui::TextWrapped("Ensure your microphone is accessible.");
#endif
    }

    display_settings_gui.render(display_mgr);
    sunshine_settings_gui.render(sunshine_webui, config);

    if (ImGui::BeginPopupModal("About Moonmic", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Moonmic v%s", MOONMIC_VERSION);
        ImGui::Separator();

        ImGui::Text("Real-time microphone streaming for PS Vita and other platforms");
        ImGui::Spacing();

        ImGui::Text("Author:");
        ImGui::SameLine();
        if (ImGui::SmallButton("AorsiniYT")) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", "https://github.com/AorsiniYT", NULL, NULL, SW_SHOWNORMAL);
#else
            system("xdg-open https://github.com/AorsiniYT &");
#endif
        }

        ImGui::Text("GitHub:");
        ImGui::SameLine();
        if (ImGui::SmallButton("moonmic")) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", "https://github.com/AorsiniYT/moonmic", NULL, NULL, SW_SHOWNORMAL);
#else
            system("xdg-open https://github.com/AorsiniYT/moonmic &");
#endif
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        if (ImGui::Button("Donate on Ko-fi", ImVec2(200, 30))) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", "https://ko-fi.com/aorsini", NULL, NULL, SW_SHOWNORMAL);
#else
            system("xdg-open https://ko-fi.com/aorsini &");
#endif
        }
        ImGui::PopStyleColor(2);

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(200, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (g_update_available && !g_update_dismissed) {
        ImGui::OpenPopup("Update Available");
    }

    if (ImGui::BeginPopupModal("Update Available", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("New Version Available!");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Current Version: %s", VersionChecker::getCurrentVersion().c_str());
        ImGui::Text("Latest Version:  %s", g_latest_version.c_str());
        ImGui::Spacing();

        ImGui::TextWrapped("A new version of Moonmic is available. Click Download to visit the releases page.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.9f, 0.3f, 1.0f));
        if (ImGui::Button("Download", ImVec2(120, 30))) {
#ifdef _WIN32
            ShellExecuteA(NULL, "open", g_download_url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
            std::string cmd = "xdg-open " + g_download_url + " &";
            system(cmd.c_str());
#endif
            g_update_dismissed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(2);

        ImGui::SameLine();

        if (ImGui::Button("Dismiss", ImVec2(120, 30))) {
            g_update_dismissed = true;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    if (is_installing) {
        if (!ImGui::IsPopupOpen("Installing Driver")) {
            ImGui::OpenPopup("Installing Driver");
        }
    }
    if (is_uninstalling) {
        if (!ImGui::IsPopupOpen("Uninstalling Driver")) {
            ImGui::OpenPopup("Uninstalling Driver");
        }
    }

    ImGui::End();
}

int main_gui(int, char*[]) {

    SingleInstance single_instance("Moonmic host by AorsiniYT");
    if (single_instance.isAnotherInstanceRunning()) {
        std::cout << "[Main] Another instance is already running" << std::endl;
        single_instance.bringExistingToFront();
        return 0;
    }

    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(600, 700, "Moonmic host by AorsiniYT", NULL, NULL);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    setWindowIcon(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    Config config;
    std::string config_path = Config::getDefaultConfigPath();
    if (!config.load(config_path)) {
        std::cerr << "[Config] Failed to load from: " << config_path << std::endl;
        std::cerr << "[Config] Using default configuration" << std::endl;
    } else {
        std::cout << "[Config] Loaded from: " << config_path << std::endl;
    }

#ifdef _WIN32

    AudioDeviceManager deviceManager;
    AudioDeviceInfo currentDefault = deviceManager.getCurrentDefaultRecordingDevice();

    if (!currentDefault.id.empty() && !currentDefault.is_virtual) {
        if (config.audio.original_mic_id.empty()) {
            config.audio.original_mic_id = currentDefault.id;
            config.save(config_path);
            std::cout << "[Main] Saved original microphone: " << currentDefault.name << std::endl;
        }
    }

    if (!config.audio.original_mic_id.empty()) {
        if (GuardianLauncher::launchGuardian(config.audio.original_mic_id, currentDefault.name)) {
            std::cout << "[Main] Guardian watchdog activated" << std::endl;
        } else {
            std::cerr << "[Main] Warning: Guardian watchdog failed to start" << std::endl;
        }
    }
#endif

    AudioReceiver receiver;
    SunshineIntegration sunshine(config);
    SunshineWebUI sunshine_webui(config);
    DisplayManager display_mgr;
    DisplaySettingsGUI display_settings_gui;
    SunshineSettingsGUI sunshine_settings_gui;
    DebugGUI debug_gui;

    receiver.setSunshineWebUI(&sunshine_webui);
    receiver.setDisplayManager(&display_mgr);

    DebugGUI::showConsole(g_debug_mode);

#ifdef _WIN32

    if (moonmic::platform::windows::IsRunningAsAdmin()) {
        std::string driverName = config.audio.driver_device_name;
        if (!driverName.empty()) {

            bool isSteam = (driverName.find("Steam") != std::string::npos);
            if (isSteam || driverName.find("VB-") != std::string::npos) {
                std::cout << "[Main] Ensuring driver is enabled: " << driverName << std::endl;
                moonmic::platform::windows::ChangeDeviceState(driverName, true);

                if (isSteam) {
                    DriverInstaller installer;
                    installer.disableSteamStreamingSpeakers();
                }
            }
        }
    }
#endif

    std::cout << "[Main] Starting receiver..." << std::endl;
    if (!receiver.start(config)) {
        std::cerr << "[Main] Failed to start receiver" << std::endl;
    }

    g_receiver = &receiver;

    VersionChecker version_checker;
    version_checker.checkForUpdates([](const VersionChecker::VersionInfo& info) {
        g_update_check_done = true;
        if (info.update_available) {
            std::cout << "[VersionChecker] Update available: " << info.latest_version << std::endl;
            g_update_available = true;
            g_latest_version = info.latest_version;
            g_download_url = info.download_url;
        } else {
            std::cout << "[VersionChecker] No update available (current: " << info.current_version << ")" << std::endl;
        }
    });

    while (!glfwWindowShouldClose(window) && g_running) {

        auto temp_stats = receiver.getStats();
        bool is_active = temp_stats.is_receiving || g_debug_mode || g_update_available;

        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {

            glfwWaitEventsTimeout(1.0);
        } else if (is_active || debug_gui.isVisible()) {

            glfwPollEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } else {

            glfwWaitEventsTimeout(0.1);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        static auto last_time = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float delta_time = std::chrono::duration<float>(now - last_time).count();
        last_time = now;

        auto receiver_stats = receiver.getStats();
        bool connected = !receiver_stats.last_sender_ip.empty();
        bool receiving = receiver_stats.is_receiving;

        AudioStats stats;
        stats.packets_received = receiver_stats.packets_received;
        stats.packets_dropped = receiver_stats.packets_dropped;
        stats.packets_dropped_lag = receiver_stats.packets_dropped_lag;
        stats.bytes_received = receiver_stats.bytes_received;
        stats.last_sender_ip = receiver_stats.last_sender_ip;
        stats.client_name = receiver_stats.client_name;
        stats.is_receiving = receiver_stats.is_receiving;
        stats.rtt_ms = receiver_stats.rtt_ms;

        // Update debug GUI (must be called every frame for animations)
        debug_gui.update(delta_time, stats, connected, receiving);

        renderGUI(window, receiver, sunshine, sunshine_webui, display_mgr, display_settings_gui, sunshine_settings_gui,
                  debug_gui, config);

        debug_gui.render();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    receiver.stop();

#ifdef _WIN32
    if (!g_restart_requested) {

        GuardianLauncher::signalNormalShutdown();
    }
#endif

#ifdef _WIN32

    if (!g_restart_requested && config.audio.auto_set_default_mic) {
        if (moonmic::platform::windows::IsRunningAsAdmin()) {
            if (!config.audio.original_mic_id.empty()) {
                moonmic::platform::windows::SetDefaultRecordingDevice(config.audio.original_mic_id);
                config.audio.original_mic_id = "";
                config.save(Config::getDefaultConfigPath());
            }

            std::cout << "[Main] Disabling Virtual Device Driver..." << std::endl;
            moonmic::platform::windows::ChangeDeviceState(config.audio.driver_device_name, false);
        }
    }
#endif

    std::cout << "[Main] Saving configuration on exit..." << std::endl;
    config.save(config_path);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

#endif

int main_console(int argc, char* argv[]) {
    std::cout << "=== Moonmic Host ===" << std::endl;
    std::cout << "Version: " << MOONMIC_VERSION << std::endl << std::endl;

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--install-driver") {
#ifdef _WIN32
            std::cout << "[Main] Installing VB-CABLE driver..." << std::endl;
            DriverInstaller installer;

            if (!DriverInstaller::isRunningAsAdmin()) {
                std::cout << "[Main] Requesting administrator privileges..." << std::endl;
                if (DriverInstaller::restartAsAdmin()) {
                    return 0;
                } else {
                    std::cerr << "[Main] Failed to restart as administrator" << std::endl;
                    return 1;
                }
            }

            if (installer.installVBCable()) {
                std::cout << "[Main] Driver installation completed" << std::endl;
                std::cout << "[Main] Please reboot your computer" << std::endl;
                return 0;
            } else {
                std::cerr << "[Main] Driver installation failed" << std::endl;
                return 1;
            }
#else
            std::cerr << "[Main] --install-driver is only supported on Windows" << std::endl;
            return 1;
#endif
        }
    }

    Config config;
    std::string config_path = Config::getDefaultConfigPath();

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--config" && i + 1 < argc) {
            config_path = argv[i + 1];
            i++;
        } else if (std::string(argv[i]) == "--debug") {
            g_debug_mode = true;
            std::cout << "[Main] Debug mode enabled (verbose logging)" << std::endl;
        }
    }

    if (!config.load(config_path)) {
        std::cout << "[Main] Using default configuration" << std::endl;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    AudioReceiver receiver;
    g_receiver = &receiver;

    if (!receiver.start(config)) {
        std::cerr << "[Main] Failed to start receiver" << std::endl;
        return 1;
    }

    std::cout << "[Main] Press Ctrl+C to stop" << std::endl;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        auto stats = receiver.getStats();
        if (stats.is_receiving) {
            std::cout << "[Stats] Packets: " << stats.packets_received << " | Dropped: " << stats.packets_dropped
                      << " | From: " << stats.last_sender_ip << std::endl;
        }
    }

    receiver.stop();

#ifdef _WIN32

    GuardianLauncher::signalNormalShutdown();
#endif
    std::cout << "[Main] Shutdown complete" << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {

    moonmic::Logger::instance().init();

    std::cout << "moonmic-host starting..." << std::endl;
    std::cout.flush();

    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--debug") {
            g_debug_mode = true;
            break;
        }
    }

#ifdef _WIN32

    if (!moonmic::DriverInstaller::isRunningAsAdmin()) {
        std::cout << "[Main] Administrator privileges required. Requesting elevation..." << std::endl;
        if (moonmic::DriverInstaller::restartAsAdmin()) {
            return 0;
        } else {
            std::cerr << "[Main] Failed to restart as Administrator." << std::endl;
            std::cerr << "[Main] Please run this application as Administrator." << std::endl;
            return 1;
        }
    }
#endif

    try {
#ifdef USE_IMGUI

        bool use_gui = true;
        for (int i = 1; i < argc; i++) {
            if (std::string(argv[i]) == "--no-gui") {
                use_gui = false;
                break;
            }
        }

        std::cout << "GUI mode: " << (use_gui ? "enabled" : "disabled") << std::endl;

        if (use_gui) {
            return main_gui(argc, argv);
        }
#endif

        return main_console(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}
