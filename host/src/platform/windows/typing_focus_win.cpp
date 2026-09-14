#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>

#include "moonmic.h"
#include "typing_focus.h"

namespace moonmic {
namespace {

bool normalizePoint(const POINT& point, HMONITOR monitor, TypingFocus& focus) {
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (!monitor || !GetMonitorInfo(monitor, &info)) {
        return false;
    }

    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    const double x =
        std::clamp(static_cast<double>(point.x - info.rcMonitor.left) / static_cast<double>(width), 0.0, 1.0);
    const double y =
        std::clamp(static_cast<double>(point.y - info.rcMonitor.top) / static_cast<double>(height), 0.0, 1.0);
    focus.normalized_x = static_cast<std::uint16_t>(std::lround(x * 65535.0));
    focus.normalized_y = static_cast<std::uint16_t>(std::lround(y * 65535.0));
    return true;
}

} // namespace

bool getTypingFocus(TypingFocus& focus) {
    const HWND foreground = GetForegroundWindow();
    if (foreground) {
        GUITHREADINFO threadInfo = {};
        threadInfo.cbSize = sizeof(threadInfo);
        const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
        if (threadId != 0 && GetGUIThreadInfo(threadId, &threadInfo) && threadInfo.hwndCaret) {
            POINT caret = {threadInfo.rcCaret.left,
                           threadInfo.rcCaret.top + (threadInfo.rcCaret.bottom - threadInfo.rcCaret.top) / 2};
            if (ClientToScreen(threadInfo.hwndCaret, &caret)) {
                const HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
                if (normalizePoint(caret, monitor, focus)) {
                    focus.source = MOONMIC_FOCUS_SOURCE_CARET;
                    return true;
                }
            }
        }
    }

    POINT pointer = {};
    if (!GetCursorPos(&pointer)) {
        return false;
    }

    if (!normalizePoint(pointer, MonitorFromPoint(pointer, MONITOR_DEFAULTTONEAREST), focus)) {
        return false;
    }
    focus.source = MOONMIC_FOCUS_SOURCE_POINTER;
    return true;
}

} // namespace moonmic
