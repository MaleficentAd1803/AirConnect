# AirConnect

A native AirPlay receiver for Windows 10 and Windows 11 (x64), with an AMOLED
black interface. C++20, Win32, Direct2D/DirectWrite, Direct3D 11 and WASAPI.

[Download the installer or portable ZIP](https://github.com/MaleficentAd1803/AirConnect/releases/latest)

**Development release:** local protocol, codec, graphics, audio and packaging
tests are documented in [VALIDATION](docs/VALIDATION.md). No Apple device was
available for the final live mirroring check. See [known limitations](docs/LIMITATIONS.md).

## Use

Install with Setup, or extract the entire portable ZIP and open `AirConnect.exe`.
Keep the supplied DLLs beside the executable. No development tools are needed.

On an iPhone or iPad on the same local network, open Control Center → Screen
Mirroring and choose the receiver name. No companion iOS app is required.
Discovery and receiving use the local network. Accounts, cloud infrastructure,
telemetry and recording by default are absent.

Allow network access only on your trusted Private network. Guest Wi-Fi isolation
and VPN routing may prevent discovery. No router forwarding is needed.

## Viewer

- One auto-hiding **⋯** menu contains mute, screenshot, recording, fullscreen,
  always-on-top, fit-to-phone, statistics and disconnect.
- The normal window follows the incoming phone's aspect ratio and rotation.
- Double-click or F/F11 toggles fullscreen; Escape exits it.
- M mutes playback, S saves a PNG, and R starts/stops recording.
- Main-window and viewer positions and normal sizes survive relaunch. Saved
  positions are brought back on-screen if a monitor is disconnected.
- Closing the viewer hides it. Closing the main window keeps receiving by
  default; choose Exit in the tray menu to shut down completely.

Screenshots retain the incoming resolution. Explicitly initiated recordings
include received audio and use fragmented MP4. Recording keeps its initial
canvas when the phone rotates; playback mute does not mute the recording.

## Updates

To upgrade an installed **0.1.0** copy, exit it from the tray and run the new
installer once. Uninstallation is unnecessary; settings, identity and recordings
are preserved. Stale files listed by the old installer are removed safely.

From **0.2.0** onward: Settings → About → Check for updates → Download update →
Install and restart. Downloads are verified against GitHub's SHA-256 digest.
The update waits for the receiver to shut down and finish recording before
replacing files. A failed file replacement restores the previous files.
Checks and downloads occur only when requested. Portable users can check for
releases and replace their extracted application folder with the new portable ZIP.

## Data and diagnostics

Preferences, encrypted receiver identity, window placement and logs live in
`%LOCALAPPDATA%\AirConnect`. Recordings default to
`%USERPROFILE%\Videos\AirConnect`. Upgrades and uninstall preserve these folders.

Optional statistics distinguish received, decoded, rendered and actually
presented frames. Local processing time excludes sender capture and network
transit. CPU/GPU utilization and performance figures are not invented.

## Build and license

See [BUILD](docs/BUILD.md), [architecture](docs/ARCHITECTURE.md) and
[third-party notices](THIRD_PARTY_NOTICES.md). AirConnect is GPL-3.0 and retains
AirPlay-Windows commit `f739810e35887cf572b69275a52dc6f32323d65d`.
Every release includes application and corresponding dependency source archives.
This is an unofficial receiver; protected streaming-service content is unsupported.
