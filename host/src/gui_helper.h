
#pragma once

#include <string>

namespace moonmic {

void ShowHelpTooltip(const char* text);

void ShowDetailedTooltip(const char* title, const char* description);

namespace Tooltips {

extern const char* VIRTUAL_MIC;
extern const char* ORIGINAL_MIC_SELECTOR;
extern const char* CURRENT_DEFAULT_MIC;

extern const char* VBCABLE_DRIVER;
extern const char* STEAM_DRIVER;
extern const char* INSTALL_DRIVER;
extern const char* UNINSTALL_DRIVER;

extern const char* AUDIO_RECEIVER_STATUS;
extern const char* SAMPLE_RATE;
extern const char* BUFFER_SIZE;

extern const char* SUNSHINE_AUTO_CONFIG;
extern const char* SUNSHINE_WEBUI;
extern const char* RELOAD_SUNSHINE;

extern const char* GUARDIAN_STATUS;

extern const char* DEBUG_MODE;
extern const char* SPEAKER_MODE;

extern const char* WHITELIST;
extern const char* PORT_CONFIG;
extern const char* CHANNELS_CONFIG;

extern const char* PAUSE_RESUME;
extern const char* DISPLAY_SETTINGS;
extern const char* PACKET_STATS;
} // namespace Tooltips

} // namespace moonmic
