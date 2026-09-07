
#ifdef USE_IMGUI

#include "sunshine_settings_gui.h"
#include "sunshine_integration.h"
#include "sunshine_webui.h"
#include "config.h"
#include <imgui.h>
#include <iostream>

namespace moonmic {

SunshineSettingsGUI::SunshineSettingsGUI() : is_open_(false) {
}

void SunshineSettingsGUI::open() {
    is_open_ = true;
}

void SunshineSettingsGUI::close() {
    is_open_ = false;
}

void SunshineSettingsGUI::render(SunshineWebUI& webui, Config& config) {
    if (!is_open_) {
        return;
    }

    ImGui::OpenPopup("Sunshine Settings");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Sunshine Settings", &is_open_, ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Sunshine Integration Settings");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Web UI Authentication");
        if (webui.isLoggedIn()) {
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "Logged in as: %s", config.sunshine.webui_username.c_str());

            ImGui::Spacing();

            auto clients = webui.getPairedClients();
            ImGui::Text("Clients in Sunshine: %zu", clients.size());

            if (!clients.empty()) {
                ImGui::BeginChild("ClientsList", ImVec2(0, 100), true);
                for (const auto& client : clients) {
                    ImGui::Text("• %s", client.name.c_str());
                }
                ImGui::EndChild();
            }

            ImGui::Spacing();

            if (ImGui::Button("Logout")) {
                webui.logout();
                std::cout << "[SunshineSettingsGUI] Logged out from Sunshine Web UI" << std::endl;
            }
        } else {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Not logged in");
            ImGui::Text("Click 'Login to Sunshine Web UI' in main window");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Configuration");

        char host_buf[256];
        strncpy(host_buf, config.sunshine.host.c_str(), sizeof(host_buf) - 1);
        host_buf[sizeof(host_buf) - 1] = '\0';
        if (ImGui::InputText("Sunshine Host", host_buf, sizeof(host_buf))) {
            config.sunshine.host = host_buf;
        }

        if (ImGui::InputInt("Sunshine Port", &config.sunshine.port)) {
            if (config.sunshine.port < 1 || config.sunshine.port > 65535) {
                config.sunshine.port = 47989;
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(120, 0))) {
            close();
        }

        ImGui::EndPopup();
    }
}

} // namespace moonmic

#endif
