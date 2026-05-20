# moonmic Build Instructions

## Unified Build System

The moonmic package uses a unified build system that compiles both the client library and host applications.

## Quick Start

### Build Everything (Current Platform)

```bash
cd third_party/moonmic
mkdir build && cd build
cmake ..
make
```

This will create:
- `moonmic.a` - Client library
- `host-<platform>/moonmic-host` - Host application for your platform

### Build Structure

```
build/
├── moonmic.a              # Client library
├── host-windows/             # Windows host (if built)
│   ├── moonmic-host.exe
│   ├── moonmic-host.json
│   └── driver/               # VB-CABLE driver
└── host-linux/               # Linux host (if built)
    ├── moonmic-host
    └── moonmic-host.json
```

## Build Options

### Client Only

```bash
cmake .. -DBUILD_HOST=OFF
make
```

### Host Only

```bash
cmake .. -DBUILD_CLIENT=OFF
make
```

### Cross-Compile Both Hosts

```bash
# Build both Windows and Linux hosts
cmake .. -DBUILD_HOST_WINDOWS=ON -DBUILD_HOST_LINUX=ON
make
```

**Note**: Cross-compilation requires appropriate toolchains installed.

## Platform-Specific Builds

### Windows

```bash
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
mingw32-make
```

Output: `build/host-windows/moonmic-host.exe`

### Linux

```bash
mkdir build && cd build
cmake ..
make
```

Output: `build/host-linux/moonmic-host`

### PS Vita (Client Only)

```bash
mkdir build && cd build
cmake -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake \
      -DBUILD_HOST=OFF \
      ..
make
```

Output: `build/moonmic.a`

## Dependencies

### Client Library

- **Opus** (encoding)
- **Platform audio API**:
  - PS Vita: SceAudio (included in VITASDK)
  - Windows: WASAPI (included in Windows SDK)
  - Linux: PulseAudio (`libpulse-dev`)

### Host Application

- **Opus** (decoding)
- **GLFW** (GUI, optional)
- **nlohmann/json** (configuration)
- **Platform audio API**:
  - Windows: WASAPI + VB-CABLE driver
  - Linux: PulseAudio

### Install Dependencies

**Ubuntu/Debian**:
```bash
sudo apt-get install libopus-dev libpulse-dev libglfw3-dev
```

**Fedora/RHEL**:
```bash
sudo dnf install opus-devel pulseaudio-libs-devel glfw-devel
```

**Windows** (vcpkg):
```bash
vcpkg install opus:x64-windows glfw3:x64-windows
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_CLIENT` | ON | Build client library |
| `BUILD_HOST` | ON | Build host application |
| `BUILD_HOST_WINDOWS` | OFF | Cross-compile Windows host |
| `BUILD_HOST_LINUX` | OFF | Cross-compile Linux host |
| `USE_IMGUI` | ON | Build host with GUI |

## Examples

### Build Client + Linux Host

```bash
cmake .. -DBUILD_HOST_WINDOWS=OFF
make
```

### Build Client + Both Hosts

```bash
cmake .. -DBUILD_HOST_WINDOWS=ON -DBUILD_HOST_LINUX=ON
make
```

### Build Without GUI (Console Only)

```bash
cmake .. -DUSE_IMGUI=OFF
make
```

## Testing

After building, test the host:

```bash
# Linux
./build/host-linux/moonmic-host --no-gui

# Windows
./build/host-windows/moonmic-host.exe --no-gui
```

## Distribution

Package the built host application:

```bash
# Linux
cp -r build/host-linux moonmic-host-linux
tar -czf moonmic-host-linux.tar.gz moonmic-host-linux/

# Windows
cp -r build/host-windows moonmic-host-windows
# Ensure driver/ folder is included
cp -r host/driver build/host-windows/
zip -r moonmic-host-windows.zip moonmic-host-windows/
```

## Troubleshooting

### Opus not found

```
CMake Error: Opus library not found
```

**Solution**: Install Opus development package:
- Linux: `sudo apt-get install libopus-dev`
- Windows: Use vcpkg or download from https://opus-codec.org/

### GLFW not found

```
CMake Error: Could not find glfw3
```

**Solution**: Install GLFW:
- Linux: `sudo apt-get install libglfw3-dev`
- Windows: `vcpkg install glfw3:x64-windows`

### PulseAudio not found (Linux)

```
CMake Error: libpulse-simple not found
```

**Solution**: `sudo apt-get install libpulse-dev`

## Integration with vita-moonlight

The client library is automatically built when building vita-moonlight:

```bash
cd /path/to/vita-moonlight
mkdir build && cd build
cmake ..
make
```

moonmic will be compiled and linked automatically.
