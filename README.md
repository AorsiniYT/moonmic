# Moonmic

Moonmic sends microphone audio from a Moonlight client to a host over UDP. The
client library captures and encodes audio; `moonmic-host` receives it and writes
it to a virtual microphone.

## Components

- `moonmic`: static client library for PS Vita, Windows, and Linux.
- `moonmic-host`: Windows and Linux receiver with GUI and console modes.
- `moonmic-guardian`: Windows watchdog that restores the previous microphone if
  the host exits unexpectedly.

The default UDP port is `48100`. Audio is sent as Opus or signed 16-bit PCM.
Protocol declarations shared by clients and the host are in
[`moonmic_protocol.h`](include/moonmic_protocol.h).

## Layout

```text
codec/       Client codecs
include/     Public client and protocol headers
network/     Client transport
platform/    Client platform implementations
src/         Client implementation and private headers
host/        Host application, resources, and virtual audio drivers
```

Dependencies used by the host are Git submodules under `host/third_party`.

## Client build

```bash
cmake -S . -B build -DBUILD_CLIENT=ON
cmake --build build --target moonmic
```

The client requires Opus and the platform audio API. PS Vita builds also require
VitaSDK. A parent CMake project can use:

```cmake
add_subdirectory(third_party/moonmic)
target_link_libraries(your_target PRIVATE moonmic::moonmic)
```

See [INTEGRATION.md](INTEGRATION.md) for the client API and
[host/README.md](host/README.md) for host setup.

## Host packages

The manual `Create release` GitHub Actions workflow reads `VERSION`, builds the
Windows and Linux hosts, and publishes:

- `moonmic-host-<version>-windows-x86_64.zip`
- `moonmic-host-<version>-linux-x86_64.tar.gz`

## License

Moonmic is distributed under the terms in [LICENSE](LICENSE). Bundled drivers
and submodules retain their respective licenses.

## Credits

- Valve Corporation — Steam Streaming Microphone driver
- VB-Audio Software — VB-CABLE driver
- Xiph.Org — Opus and SpeexDSP
- Dear ImGui and GLFW — host interface
- AorsiniYT — Moonmic and vita-moonlight integration
