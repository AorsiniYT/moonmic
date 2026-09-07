
#pragma once

#ifdef USE_IMGUI

#include <string>

namespace moonmic {

class SunshineWebUI;
struct Config;

class SunshineSettingsGUI {
  public:
    SunshineSettingsGUI();

    void render(SunshineWebUI& webui, Config& config);

    void open();

    void close();

    bool isOpen() const { return is_open_; }

  private:
    bool is_open_;
};

} // namespace moonmic

#endif
