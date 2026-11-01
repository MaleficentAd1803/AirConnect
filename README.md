# AirConnect 0.1.0

Native Windows AirPlay receiver built in C++20 with AMOLED dark as the default.
The interface uses Win32,
Direct2D and DirectWrite; video uses Direct3D 11 and audio uses WASAPI.
There is no web frontend, browser runtime, cloud service, account or telemetry.

**This is an unsigned validation build. No iPhone or iPad was available during
development. Live Apple-device discovery, mirroring and audio compatibility
remain untested. Read VALIDATION.md and LIMITATIONS.md before relying on it.**

## Run

Use the Setup executable for a per-user installation, or extract the complete
portable ZIP and open `AirConnect.exe`. Keep all supplied DLLs beside the EXE.
No development tools are required to run the packaged application.

The home screen shows the receiver name and the actual discovery result. On an
iPhone or iPad on the same local network, open Control Center → Screen Mirroring
and choose the receiver name. A separate native viewer opens when frames arrive.

Windows may ask about network access. AirPlay needs inbound access on your
trusted **Private** network. Guest Wi-Fi isolation and VPN routing can prevent
discovery. Do not forward router ports. The source distribution contains an
optional, administrator-only `tools/allow-private-firewall.ps1` that creates one
Private-profile application rule restricted to the local subnet. No firewall
rule was installed automatically during development.

## Controls

- Settings: receiver name, appearance, stream size and frame-rate hints, hardware
  decoding, HEVC, VSync, output device, volume/mute, recording and diagnostics.
- Viewer: fullscreen, screenshot, Record/Stop, Pin, mute, statistics, Disconnect.
- Double-click or F/F11: toggle fullscreen. Escape: leave fullscreen.
- R: start/stop recording. S: PNG screenshot. M: mute playback.
- Close the viewer to hide it. Use Open viewer in the home screen to restore it.
- Closing the home window keeps receiving by default. Choose Exit in the tray
  menu to stop all listeners, media workers and advertisements.

Recording begins only after an explicit Record action. Recordings include
received audio, preserve their initial canvas through orientation changes, and
use fragmented MP4 to improve recovery after interruption. A stopped recording
is finalized before exit. Screenshots preserve the received frame resolution.
Playback mute does not mute recordings. There is no microphone/system-audio
capture. Incoming video is re-encoded for recording; compressed passthrough is
not implemented.

Settings and the DPAPI-encrypted receiver identity live in
`%LOCALAPPDATA%\AirConnect`. The default capture folder is
`%USERPROFILE%\Videos\AirConnect`. Uninstall keeps both. Source and build
instructions are in the companion source archive and BUILD.md.

## Statistics

Receive/Decode/Present are separate measured rates. Present counts successful
DXGI presentations, not fabricated frames. A hidden/occluded window can render
frames without presenting them. Local processing latency starts after the
encrypted mirror-frame body arrives at the PC; it excludes capture, network
transit and exact scan-out. CPU/GPU utilization is not estimated or invented.

## License and provenance

AirConnect is GPL-3.0. It retains the AirPlay-Windows protocol implementation
at `f739810e35887cf572b69275a52dc6f32323d65d`. UxPlay was inspected as a
secondary reference. See THIRD_PARTY_NOTICES.md and the complete AirConnect
source archive. Public redistribution needs the dependency-source/license
review described in LIMITATIONS.md; the current packages are for local
validation on this PC. DRM-protected streaming services are outside scope.
