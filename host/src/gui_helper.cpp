
#include "gui_helper.h"

#ifdef USE_IMGUI
#include <imgui.h>
#endif

namespace moonmic {

void ShowHelpTooltip(const char* text) {
#ifdef USE_IMGUI
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
#endif
}

void ShowDetailedTooltip(const char* title, const char* description) {
#ifdef USE_IMGUI
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);

        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s", title);
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted(description);

        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
#endif
}

namespace Tooltips {
const char* VIRTUAL_MIC = "Enable virtual microphone mode.\n"
                          "When enabled, the virtual mic (Steam/VBCable) becomes the system default.\n"
                          "Your original mic will be restored when the app closes.";

const char* ORIGINAL_MIC_SELECTOR = "Select which microphone to restore when the app closes.\n"
                                    "This is your 'physical' microphone that will become the default\n"
                                    "when you exit moonmic-host or if the app crashes unexpectedly.\n\n"
                                    "The guardian watchdog ensures restoration even on crashes.";

const char* CURRENT_DEFAULT_MIC = "Shows the currently active default recording device.\n"
                                  "This is what Windows applications will use for microphone input.";

const char* VBCABLE_DRIVER = "VB-Audio Virtual Cable driver.\n"
                             "Provides a virtual audio device for routing audio between applications.\n"
                             "Supports WASAPI and WDM-KS modes.";

const char* STEAM_DRIVER = "Steam Streaming Microphone driver.\n"
                           "Low-latency virtual microphone designed for game streaming.\n"
                           "Uses WDM-KS with low buffering overhead.";

const char* INSTALL_DRIVER = "Install the selected virtual audio driver.\n"
                             "Requires administrator privileges.\n"
                             "A system reboot is recommended after installation.";

const char* UNINSTALL_DRIVER = "Remove the virtual audio driver from the system.\n"
                               "The app will automatically stop audio playback before uninstalling\n"
                               "to prevent conflicts. Requires administrator privileges.";

const char* AUDIO_RECEIVER_STATUS = "Shows the current state of the audio receiver.\n"
                                    "Running: Listening for audio from Sunshine client\n"
                                    "Stopped: Not receiving audio";

const char* SAMPLE_RATE = "Audio sample rate in Hz.\n"
                          "48000 Hz is recommended for best quality.\n"
                          "Lower rates (16000 Hz) may be required for some virtual devices.";

const char* BUFFER_SIZE = "Audio buffer size affects latency and stability.\n"
                          "Smaller buffers = lower latency but may cause audio glitches.\n"
                          "Larger buffers = more stable but higher latency.";

const char* SUNSHINE_AUTO_CONFIG = "Automatically configure Sunshine when connected.\n"
                                   "Updates host IP and audio settings for optimal streaming.\n"
                                   "Also handles resolution switching based on client requests.";

const char* SUNSHINE_WEBUI = "Access Sunshine's Web Interface for advanced configuration.\n"
                             "Login is required to allow Moonmic to change host resolution\n"
                             "to match the PS Vita client (e.g. 960x544 or 1280x720).";

const char* RELOAD_SUNSHINE = "Reload the paired client list from Sunshine WebUI.\n"
                              "Use this after pairing a new device.";

const char* GUARDIAN_STATUS = "Guardian Watchdog Status.\n"
                              "The Guardian is a separate process that monitors this app.\n"
                              "If Moonmic crashes, Guardian will automatically restore your\n"
                              "original microphone to prevent audio issues.";

const char* DEBUG_MODE = "Enable verbose logging and debug console.\n"
                         "Useful for troubleshooting connection or audio issues.\n"
                         "Shows detailed packet info and driver status.";

const char* SPEAKER_MODE = "Debug Mode: Route audio to system speakers instead of virtual mic.\n"
                           "WARNING: This will cause echo if used during a call!\n"
                           "Use only for testing if audio is being received correctly.";

const char* WHITELIST = "Security: Only allow connections from known Sunshine clients.\n"
                        "When enabled, only devices paired in Sunshine can send audio.\n"
                        "Disable to allow any device on the network to connect (less secure).";

const char* PORT_CONFIG = "UDP Port to listen for audio stream.\n"
                          "Default: 48100. Must match the port configured in the Vita client.\n"
                          "Ensure this port is open in your firewall.";

const char* CHANNELS_CONFIG = "Audio Channels (1 = Mono, 2 = Stereo).\n"
                              "Mono is recommended for voice chat to save bandwidth.\n"
                              "Stereo provides better quality but requires more data.";

const char* PAUSE_RESUME = "Temporarily stop/start the audio receiver.\n"
                           "Pausing stops the audio stream but keeps the connection alive.\n"
                           "Useful if you need to mute the mic quickly.";

const char* DISPLAY_SETTINGS = "Configure how Moonmic handles host resolution.\n"
                               "You can enable/disable auto-resolution switching when\n"
                               "the Vita client connects.";

const char* PACKET_STATS = "Real-time packet statistics.\n"
                           "Received: Total packets successfully received\n"
                           "Dropped: Packets lost due to network issues\n"
                           "Lag: Packets dropped due to processing delays";
} // namespace Tooltips

} // namespace moonmic
