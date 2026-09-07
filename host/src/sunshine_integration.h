
#pragma once

#include <string>

namespace moonmic {

struct Config;

class SunshineIntegration {
  public:
    SunshineIntegration(Config& config);

    bool isPaired() const;

    void reload();

  private:
    Config& config_;
};

} // namespace moonmic
