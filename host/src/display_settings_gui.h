
#pragma once

#ifdef USE_IMGUI

#include "display_manager.h"

namespace moonmic {

class DisplaySettingsGUI {
  public:
    DisplaySettingsGUI();

    void render(DisplayManager& display_mgr);

    void open();

    void close();

    bool isOpen() const { return is_open_; }

  private:
    bool is_open_;
    int selected_width_;
    int selected_height_;
    int selected_refresh_;
    bool auto_change_enabled_;
};

} // namespace moonmic

#endif
