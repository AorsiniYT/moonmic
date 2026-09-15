# Third-party notices

Moonmic's MIT license applies only to Moonmic's own source code. Components
under `host/third_party` retain their upstream licenses:

| Component | License file |
| --- | --- |
| curl | `host/third_party/curl/COPYING` |
| FFmpeg | `host/third_party/ffmpeg/LICENSE.md` and the `COPYING.*` files in that directory |
| GLFW | `host/third_party/glfw/LICENSE.md` |
| Dear ImGui | `host/third_party/imgui/LICENSE.txt` |
| JSON for Modern C++ | `host/third_party/json/LICENSE.MIT` |
| PortAudio | `host/third_party/portaudio/LICENSE.txt` |
| SpeexDSP | `host/third_party/speexdsp/COPYING` |
| stb_image | The license block at the end of `host/third_party/stb/stb_image.h` |

The signed VB-CABLE files under `host/drivers/vbaudio` are distributed by
VB-Audio Software under their own terms. See
`host/drivers/vbaudio/readme.txt` and
[VB-Audio's website](https://vb-audio.com/Cable/) for details.

The Steam Streaming Microphone driver is not bundled with Moonmic. It remains
the property of Valve Corporation and is used only when already installed on
the host system.
