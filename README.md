# Moonmic

Cross-platform microphone capture library for Moonlight clients with UDP transmission to host.

## Overview

**Moonmic** is a modular C library that enables microphone audio capture and transmission from Moonlight clients to a host application. It provides:

- **Client Library**: Captures microphone audio and transmits via UDP with Opus encoding
- **Host Application**: Receives audio and injects into virtual audio device
- **Platform Support**: PS Vita, Windows, Linux (macOS and Android ready for extension)

## Architecture

```
┌─────────────────┐         UDP          ┌──────────────────┐
│  moonmic        │ ───────────────────> │  moonmic-host    │
│  (Client)       │   Opus Encoded       │  (Host)          │
│                 │   Port 48100         │                  │
│ - PS Vita       │                      │ - Windows        │
│ - Windows       │                      │ - Linux          │
│ - Linux         │                      │                  │
└─────────────────┘                      └──────────────────┘
                                                  │
                                                  ▼
                                         ┌─────────────────┐
                                         │ Virtual Audio   │
                                         │ Device          │
                                         │ (WASAPI/Pulse)  │
                                         └─────────────────┘
```

## Project Structure

```
moonmic/
├── moonmic.h                    # Public C API
├── moonmic_internal.h           # Internal types
├── moonmic_client.cpp           # Main client implementation
├── heartbeat_monitor.h          # Connection heartbeat API
├── CMakeLists.txt
├── README.md
├── INTEGRATION.md               # Integration guide
├── codec/
│   └── opus_encoder.cpp         # Opus encoding
├── network/
│   └── udp_sender.cpp           # UDP transmission
├── platform/                    # Platform-specific implementations
│   ├── psvita/
│   │   ├── platform_config.h
│   │   ├── audio_capture_vita.cpp
│   │   └── heartbeat_monitor.cpp      # PS Vita connection monitor
│   ├── windows/
│   │   ├── platform_config.h
│   │   ├── audio_capture_windows.cpp
│   │   └── heartbeat_monitor.cpp      # Windows connection monitor
│   └── linux/
│       ├── platform_config.h
│       ├── audio_capture_linux.cpp
│       └── heartbeat_monitor.cpp      # Linux connection monitor
└── host/                        # Host application
    ├── CMakeLists.txt
    ├── README.md
    ├── QUICKSTART.md
    ├── drivers/                 # Virtual audio drivers
    │   ├── SVACDriver/          # Steam Streaming Mic (Recommended)
    │   └── vbaudio/             # VB-CABLE driver (Alternative)
    ├── guardian/                # Watchdog process
    │   └── main.cpp
    └── src/
        ├── main.cpp
        ├── guardian_launcher.cpp # Watchdog launcher
        ├── config.cpp
        ├── audio_receiver.cpp
        ├── platform/
        │   ├── windows/
        │   │   ├── driver_installer.cpp
        │   │   └── audio_utils.cpp
        │   └── linux/
        └── ...
```

## Features

### Client Library (moonmic)

- ✅ Simple C API
- ✅ Modular platform architecture
- ✅ Opus encoding (64 kbps mono / 96 kbps stereo)
- ✅ UDP transmission with packet validation
- ✅ **Sunshine client validation** - Certificate-based authentication
- ✅ **Secure handshake protocol** - Pre-stream client verification
- ✅ **Connection heartbeat** - Real-time connection status monitoring (<0.2% CPU)
- ✅ Low latency (10ms frames @ 48kHz)
- ✅ Auto-start capability
- ✅ Error callbacks

### Host Application (moonmic-host)

- ✅ Cross-platform (Windows, Linux)
- ✅ **Standalone executable** - No external dependencies (drivers embedded on Windows)
- ✅ **Single instance** - Prevents multiple copies with window bring-to-front
- ✅ Dear ImGui GUI + console mode
- ✅ **Auto-save configuration** - Changes persist automatically
- ✅ **Client validation** - PairStatus-based security
- ✅ **Connection monitoring** - Active heartbeat ping system for real-time status
- ✅ **Sunshine integration** - Paired clients whitelist sync (Web UI login only, PIN pairing disabled)
- ✅ **Client name display** - Shows connected device in stats
- ✅ **VB-CABLE embedded** - All driver files included in .exe (Windows)
- ✅ **One-click driver installation** - GUI button or `--install-driver` command
- ✅ Virtual audio device injection (WASAPI/PulseAudio)
- ✅ Real-time statistics and connection monitoring
- ✅ **Admin privilege handling** - Automatic UAC elevation when needed
- ✅ **Custom icon** - Professional branding

## Quick Start

### Client Integration

```cpp
#include "moonmic.h"

// Basic configuration (no validation)
moonmic_config_t config = {
    .host_ip = "192.168.1.100",
    .port = 48100,
    .sample_rate = 48000,
    .channels = 1,
    .bitrate = 64000,
    .auto_start = true
};

moonmic_client_t* mic = moonmic_create(&config);
// Transmitting automatically...

moonmic_destroy(mic);

// Advanced: With Sunshine validation
moonmic_config_t secure_config = {
    .host_ip = "192.168.1.100",
    .port = 48100,
    .sample_rate = 48000,
    .channels = 1,
    .bitrate = 64000,
    .auto_start = true,
    
    // Security (validated by application layer)
    .uniqueid = "0123456789ABCDEF",     // From uniqueid.dat
    .devicename = "PS Vita",              // Device identifier
    .pair_status = 1                      // From Sunshine validation
};

moonmic_client_t* mic = moonmic_create(&secure_config);
// Client will send handshake with PairStatus before audio
```

### Host Application

**Windows (Standalone - 7.9 MB)**
```bash
# Download latest release or build from source
moonmic-host.exe

# Install VB-CABLE driver (first-time setup)
moonmic-host.exe --install-driver

# Or use GUI - click "Install VB-CABLE Driver" button
```

**Linux**
```bash
cd host
mkdir build && cd build
cmake ..
make

# Run
./moonmic-host
```

**Configuration**
- Windows: `%APPDATA%\AorsiniYT\MoonMic\moonmic-host.json`
- Linux: `~/.config/AorsiniYT/MoonMic/moonmic-host.json`
- Auto-saves on changes (GUI toggle, brightness settings, etc.)
- Syncs with Sunshine paired clients automatically

## Audio Protocol

### Packet Format

```
┌──────────────┬──────────────┬──────────────┬─────────────┐
│ Magic (4B)   │ Sequence(4B) │ Timestamp(8B)│ Opus Data   │
│ 0x4D4D4943   │              │              │             │
└──────────────┴──────────────┴──────────────┴─────────────┘
```

### Audio Parameters

| Parameter | Value |
|-----------|-------|
| Sample Rate | 48000 Hz |
| Channels | 1 (mono) or 2 (stereo) |
| Frame Size | 480 samples (10ms @ 48kHz) |
| Bitrate | 64 kbps (mono) / 96 kbps (stereo) |
| Port | 48100 (configurable) |
| Codec | Opus (VOIP mode, CBR) |

## Platform Support

### Client Platforms

| Platform | Status | Audio API |
|----------|--------|-----------|
| PS Vita | ✅ Implemented | SceAudio |
| Windows | ✅ Implemented | WASAPI |
| Linux | ✅ Implemented | PulseAudio |
| macOS | 🔄 Ready for extension | AVFoundation |
| Android | 🔄 Ready for extension | AudioRecord |

### Host Platforms

| Platform | Status | Virtual Device |
|----------|--------|----------------|
| Windows | ✅ Implemented | Steam Streaming Mic (Recommended), VB-CABLE |
| Linux | ✅ Implemented | PulseAudio |

## Dependencies

### Client Library

- **Opus** (encoding)
- **Platform audio API** (SceAudio, WASAPI, PulseAudio)
- **Standard sockets** (UDP)

### Host Application

- **Opus** (decoding)
- **GLFW** (GUI, optional)
- **Dear ImGui** (GUI, optional)
- **nlohmann/json** (configuration)
- **Steam Streaming Microphone** (Preferred low-latency driver)
- **VB-CABLE** (Alternative virtual microphone)

## Building

See [INTEGRATION.md](INTEGRATION.md) for detailed build instructions.

### Quick Build (Linux)

```bash
# Client library
mkdir build && cd build
cmake ..
make moonmic

# Host application
cd host
mkdir build && cd build
cmake ..
make
```

## Documentation

- [INTEGRATION.md](INTEGRATION.md) - Integration guide for vita-moonlight
- [host/README.md](host/README.md) - Host application documentation
- [host/drivers/README.md](host/drivers/README.md) - Virtual Audio Drivers guide (Steam & VB-CABLE)

## Use Cases

- **Remote Gaming**: Transmit voice chat from PS Vita to PC
- **Streaming**: Use Vita microphone with OBS/Discord on PC
- **Voice Chat**: Enable voice communication in Moonlight sessions
- **Testing**: Test microphone handling without physical hardware

## License

This project is part of vita-moonlight.

VB-CABLE driver (Windows) is donationware by VB-Audio Software:
- Free for end users
- Donations welcome at: https://vb-audio.com/Cable/

## Contributing

Contributions welcome! To add a new platform:

1. Create `platform/yourplatform/audio_capture_yourplatform.cpp`
2. Implement `audio_capture_t` interface
3. Add platform detection to `CMakeLists.txt`
4. Update documentation

## Credits

- **Valve Corporation** - Steam Streaming Microphone Driver
- **VB-Audio Software** - VB-CABLE virtual audio driver
- **Xiph.Org** - Opus audio codec
- **Dear ImGui** - Immediate mode GUI library
- **AorsiniYT** - moonmic-host application and integration

## Support

If you need more help, join the [#vita-help channel in Discord](https://discord.gg/rf5pkZvpJ3).
