# Moonmic Host

`moonmic-host` receives Moonmic UDP packets, decodes their audio, and writes it
to a virtual microphone. Windows uses Steam Streaming Microphone or VB-CABLE;
Linux uses PulseAudio.

## Command-line options

```text
--config <path>    Use another configuration file
--debug            Enable verbose logging
--no-gui           Run without the graphical interface
--install-driver   Install the embedded VB-CABLE driver on Windows
```

The Windows GUI also installs or removes Steam Streaming Microphone and
VB-CABLE through Driver Manager. Driver changes require administrator access and
may require a reboot.

## Configuration

The default configuration is stored at:

- Windows: `%APPDATA%\AorsiniYT\MoonMic\moonmic-host.json`
- Linux: `~/.config/AorsiniYT/MoonMic/moonmic-host.json`

The shipped `moonmic-host.json` is used as the initial configuration. The
default listener is `0.0.0.0:48100`, and the client whitelist is enabled.

## Build from source

Initialize dependencies first:

```bash
git submodule update --init --recursive
```

Linux:

```bash
cmake -S host -B build/linux -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DHOST_TARGET_LINUX=ON \
  -DHOST_TARGET_WINDOWS=OFF \
  -DUSE_IMGUI=ON
cmake --build build/linux --target moonmic-host
```

Windows builds cross-compile from Linux using
`host/third_party/toolchain-mingw.cmake`.

## Linux desktop entry

To install the launcher and icon after building:

```bash
cmake --install build/linux
```

Installing the launcher requires administrative rights. The install prefix can
be changed at configure time with `-DCMAKE_INSTALL_PREFIX=<path>`.

## Drivers

Driver files embedded in the Windows executable are stored under
[`drivers`](drivers/README.md). Applications should select `Steam Streaming
Microphone` or `CABLE Output` as their input device after installation.
