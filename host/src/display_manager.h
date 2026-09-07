
#pragma once

#include <vector>
#include <string>

namespace moonmic {

class DisplayManager {
  public:
    struct Resolution {
        int width;
        int height;
        int refresh_rate;

        bool operator==(const Resolution& other) const {
            return width == other.width && height == other.height && refresh_rate == other.refresh_rate;
        }
    };

    DisplayManager();
    ~DisplayManager();

    Resolution getCurrentResolution();

    bool setResolution(int width, int height, int refresh_rate);

    bool restoreOriginalResolution();

    std::vector<Resolution> getSupportedResolutions();

    bool isResolutionChanged() const { return resolution_changed_; }

  private:
    Resolution original_resolution_;
    bool resolution_changed_;

#ifdef _WIN32
    bool setResolutionWindows(int width, int height, int refresh_rate);
    std::vector<Resolution> getSupportedResolutionsWindows();
#else
    bool setResolutionLinux(int width, int height, int refresh_rate);
    std::vector<Resolution> getSupportedResolutionsLinux();
#endif
};

} // namespace moonmic
