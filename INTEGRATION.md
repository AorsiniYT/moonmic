# moonmic - Integration Guide

This guide explains how to integrate moonmic into vita-moonlight and other Moonlight clients.

## Building

The library builds automatically with vita-moonlight:

```bash
cd /mnt/i/vita/Proyecto/vita-moonlight
mkdir -p build && cd build

# For PS Vita
cmake -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake ..
make

# For Windows (MinGW cross-compile)
cmake -DCMAKE_TOOLCHAIN_FILE=... ..
make

# For Linux native
cmake ..
make
```

## Dependencies

### PS Vita
- **Opus**: Included in VITASDK
- **SceAudio**: Included in VITASDK

### Windows
- **Opus**: Submodule included (builds from source)
- **WASAPI**: Included in Windows SDK

### Linux
```bash
sudo apt-get install libopus-dev libpulse-dev
```

## Basic Usage

### Simple Configuration (No Validation)

```cpp
#include "moonmic.h"

// Configuration
moonmic_config_t config = {
    .host_ip = "192.168.1.100",  // Your PC's IP address
    .port = 48100,               // Default moonmic-host port
    .sample_rate = 48000,        // 48kHz recommended
    .channels = 1,               // Mono (1) or Stereo (2)
    .bitrate = 64000,            // 64 kbps for mono, 96 kbps for stereo
    .auto_start = true           // Start transmitting immediately
};

// Create and start
moonmic_client_t* mic = moonmic_create(&config);
if (mic) {
    // Already transmitting if auto_start = true
    
    // Stop when needed
    moonmic_stop(mic);
    moonmic_destroy(mic);
}
```

### Secure Configuration (With Sunshine Validation)

```cpp
#include "moonmic.h"

// Application performs Sunshine validation first
int pair_status = validate_with_sunshine(host_ip);  // Application-specific
std::string uniqueid = read_uniqueid_from_keydir(); // Application-specific

moonmic_config_t config = {
    .host_ip = "192.168.1.100",
    .port = 48100,
    .sample_rate = 48000,
    .channels = 1,
    .bitrate = 64000,
    .auto_start = true,
    
    // Sunshine validation fields
    .uniqueid = uniqueid.c_str(),     // Client unique identifier (16 chars)
    .devicename = "PS Vita",          // Human-readable device name
    .sunshine_https_port = 47984,     // Sunshine HTTPS port
    .pair_status = pair_status        // Result from validation (0 or 1)
};

moonmic_client_t* mic = moonmic_create(&config);
// Client sends handshake packet with validation data
// Host accepts/rejects based on PairStatus and whitelist setting
```

## Integration with GameStreamClient

See `examples/integration_example.cpp` for a complete example.

Basic integration steps:

1. Include header: `#include "moonmic.h"`
2. Create instance when streaming session starts
3. Destroy when session ends
4. Handle errors via callbacks

Example:
```cpp
class GameStreamClient {
    moonmic_client_t* microphone_;
    
    void startStreaming(const char* host_ip) {
        moonmic_config_t config = {
            .host_ip = host_ip,
            .port = 48100,
            .sample_rate = 48000,
            .channels = 1,
            .bitrate = 64000,
            .auto_start = true
        };
        
        microphone_ = moonmic_create(&config);
        moonmic_set_error_callback(microphone_, on_mic_error, this);
        moonmic_set_status_callback(microphone_, on_mic_status, this);
    }
    
    void stopStreaming() {
        if (microphone_) {
            moonmic_destroy(microphone_);
            microphone_ = nullptr;
        }
    }
    
    static void on_mic_error(const char* error, void* userdata) {
        printf("[Microphone] Error: %s\n", error);
    }
    
    static void on_mic_status(bool connected, void* userdata) {
        printf("[Microphone] Status: %s\n", connected ? "Active" : "Inactive");
    }
};
```

## Callbacks

```cpp
void error_callback(const char* error, void* userdata) {
    printf("Microphone error: %s\n", error);
}

void status_callback(bool connected, void* userdata) {
    printf("Microphone status: %s\n", connected ? "Connected" : "Disconnected");
}

moonmic_set_error_callback(mic, error_callback, NULL);
moonmic_set_status_callback(mic, status_callback, NULL);
```

## Host Setup

On the host PC, run `moonmic-host` application:

### Windows
```bash
# Download from releases or build from source
moonmic-host.exe

# First-time setup: Install Steam Streaming Microphone (Recommended)
moonmic-host.exe --install-steam-driver

# Or use the GUI "Driver Manager"
# Drivers are embedded in the executable - no external download needed.
```

### Linux
```bash
cd host
mkdir build && cd build
cmake ..
make

./moonmic-host
```

**Configuration is automatic:**
- Windows: `%APPDATA%\AorsiniYT\MoonMic\moonmic-host.json`
- Linux: `~/.config/AorsiniYT/MoonMic/moonmic-host.json`
- Auto-saves when changed in GUI
- Syncs with Sunshine paired clients if enabled

## Troubleshooting

### No audio received

1. **Verify moonmic-host is running** on the PC
2. **Check firewall** - UDP port 48100 must be open
3. **Verify IP address** in client configuration matches PC IP
4. **On Windows**: Ensure VB-CABLE is installed (`moonmic-host.exe --install-driver`)
5. **Test with verbose logging**:
   ```cpp
   moonmic_set_error_callback(mic, verbose_error_handler, NULL);
   ```

### High latency

1. **Reduce buffer size** in host configuration (edit `moonmic-host.json`)
2. **Check network latency** with `ping` to host
3. **Use wired connection** instead of WiFi if possible
4. **Disable QoS/traffic shaping** on router

### Build errors

**PS Vita**
- Ensure `VITASDK` environment variable is set
- Run `source /usr/local/vitasdk/vitasdk.sh`

**Windows (Cross-compile)**
- Opus is built from submodule automatically
- Ensure MinGW toolchain is installed

**Linux**
```bash
# Install dependencies
sudo apt-get install libopus-dev libpulse-dev

# If using GLFW for host builds
sudo apt-get install libglfw3-dev libgl1-mesa-dev
```

### Runtime errors

**"Failed to open audio device"** (PS Vita)
- Check that no other app is using the microphone
- Ensure `sceAudioInOpenPort` is not failing (check logs)

**"Network error: sendto failed"** 
- Check firewall settings on both client and host
- Verify network connectivity between devices
- Ensure port 48100 UDP is not blocked

## Complete API Reference

See `moonmic.h` for full API documentation.

### Core Functions

- `moonmic_create(config)` - Create microphone instance
- `moonmic_destroy(mic)` - Destroy instance and free resources
- `moonmic_start(mic)` - Start audio transmission
- `moonmic_stop(mic)` - Stop audio transmission
- `moonmic_is_active(mic)` - Check if transmitting
- `moonmic_version()` - Get library version string

### Callbacks

- `moonmic_set_error_callback(mic, callback, userdata)` - Set error handler
- `moonmic_set_status_callback(mic, callback, userdata)` - Set status change handler

### Configuration Structure

```c
typedef struct {
    // Core audio settings
    const char* host_ip;          // Host PC IP address
    uint16_t port;                // UDP port (default: 48100)
    uint32_t sample_rate;         // Sample rate in Hz (default: 48000)
    uint8_t channels;             // 1 = mono, 2 = stereo
    uint32_t bitrate;             // Opus bitrate in bps (default: 64000)
    bool raw_mode;                // true = RAW PCM, false = Opus compression
    bool auto_start;              // Start immediately after creation
    float gain;                   // Gain multiplier (1.0-100.0, default: 10.0)
    
    // Sunshine validation (optional)
    const char* uniqueid;         // Client uniqueid (16 chars, optional)
    const char* devicename;       // Device name for identification (optional)
    int sunshine_https_port;      // Sunshine HTTPS port (default: 47984, 0=skip)
    const char* cert_path;        // Path to client.pem (optional, for reference)
    const char* key_path;         // Path to key.pem (optional, for reference)
    int pair_status;              // Pair status from validation (0=unpaired, 1=paired)
} moonmic_config_t;
```

**Validation Fields**:
- Leave `uniqueid`, `devicename`, `cert_path`, `key_path` as `NULL` to skip validation
- Set `pair_status = 0` if no validation performed
- Set `pair_status = 1` if validated with Sunshine
- If host has `enable_whitelist = true`, `pair_status` must be 1 to connect

## Performance Tips

- **Use mono (1 channel)** unless stereo is required - saves bandwidth
- **48kHz sample rate** is recommended for best quality/performance balance
- **64 kbps bitrate** for mono is sufficient for voice
- **Enable auto_start** to simplify client code
- **Use error callbacks** to handle network issues gracefully

## Credits

- **Valve Corporation** - Steam Streaming Microphone Driver
- **VB-Audio Software** - VB-CABLE virtual audio driver (https://vb-audio.com/Cable/)
- **Xiph.Org Foundation** - Opus audio codec  
- **Dear ImGui** - Host application GUI
- **AorsiniYT** - moonmic-host application and integration
