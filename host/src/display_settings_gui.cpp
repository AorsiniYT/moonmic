
#ifdef USE_IMGUI

#include "display_settings_gui.h"
#include <imgui.h>
#include <iostream>

namespace moonmic {

DisplaySettingsGUI::DisplaySettingsGUI()
    : is_open_(false), selected_width_(1920), selected_height_(1080), selected_refresh_(60),
      auto_change_enabled_(false) {
}

void DisplaySettingsGUI::open() {
    is_open_ = true;
}

void DisplaySettingsGUI::close() {
    is_open_ = false;
}

void DisplaySettingsGUI::render(DisplayManager& display_mgr) {
    if (!is_open_) {
        return;
    }

    ImGui::OpenPopup("Display Settings");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Display Settings", &is_open_, ImGuiWindowFlags_AlwaysAutoResize)) {

        ImGui::Text("Display Resolution Configuration");
        ImGui::Separator();
        ImGui::Spacing();

        auto current = display_mgr.getCurrentResolution();
        ImGui::Text("Current Resolution: %dx%d @%dHz", current.width, current.height, current.refresh_rate);

        if (display_mgr.isResolutionChanged()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "(Changed)");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Target Resolution:");
        ImGui::InputInt("Width##res", &selected_width_, 0, 0);
        ImGui::InputInt("Height##res", &selected_height_, 0, 0);
        ImGui::InputInt("Refresh Rate (Hz)##res", &selected_refresh_, 0, 0);

        ImGui::Spacing();

        ImGui::Text("Quick Presets:");
        if (ImGui::Button("1280x720@60")) {
            selected_width_ = 1280;
            selected_height_ = 720;
            selected_refresh_ = 60;
        }
        ImGui::SameLine();
        if (ImGui::Button("1920x1080@60")) {
            selected_width_ = 1920;
            selected_height_ = 1080;
            selected_refresh_ = 60;
        }
        ImGui::SameLine();
        if (ImGui::Button("2560x1440@60")) {
            selected_width_ = 2560;
            selected_height_ = 1440;
            selected_refresh_ = 60;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Checkbox("Auto-change when Sunshine session starts", &auto_change_enabled_);
        ImGui::TextWrapped(
            "When enabled, resolution will change automatically when a Sunshine streaming session is detected.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Apply Resolution", ImVec2(150, 30))) {
            if (display_mgr.setResolution(selected_width_, selected_height_, selected_refresh_)) {
                std::cout << "[DisplaySettingsGUI] Resolution applied successfully" << std::endl;
            } else {
                std::cerr << "[DisplaySettingsGUI] Failed to apply resolution" << std::endl;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Restore Original", ImVec2(150, 30))) {
            if (display_mgr.restoreOriginalResolution()) {
                std::cout << "[DisplaySettingsGUI] Resolution restored" << std::endl;
            }
        }

        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(150, 0))) {
            close();
        }

        ImGui::EndPopup();
    }
}

} // namespace moonmic

#endif
