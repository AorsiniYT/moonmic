# Windows audio drivers

Moonmic Host can write to either bundled virtual microphone:

- [Steam Streaming Microphone](SVACDriver/README.md) uses WDM-KS and is the
  preferred low-latency path.
- [VB-CABLE](vbaudio/README.md) exposes `CABLE Input` for playback and
  `CABLE Output` for recording.

The signed driver packages are embedded in `moonmic-host.exe`. Install or remove
them from Driver Manager. Administrator access is required, and Windows may ask
for a reboot.
