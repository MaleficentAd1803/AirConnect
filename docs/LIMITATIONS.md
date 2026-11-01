# Known limitations

- No iPhone or iPad was available for the final acceptance check. Live discovery,
  Apple authentication, encrypted mirroring, audible accompanying audio, rotation,
  reconnect and recording from a real sender remain unverified in this fork.
- Local protocol and generated-media tests do not establish AirPlay compatibility.
  DNS-SD registration succeeds locally; remote discovery also depends on the LAN,
  firewall, multicast routing, VPNs and guest-network isolation.
- Native UI automation could not connect to this PC's native automation pipe.
  App-generated dark/light captures were inspected, and native window state,
  rendering, resizing/fullscreen/PiP and inactivity behavior were exercised by
  the test executable. Normal keyboard/mouse interaction and physical multi-DPI
  monitor transitions still need a user check.
- DXGI has returned both S_OK and DXGI_STATUS_OCCLUDED in local test runs.
  Counters distinguish accepted presentation from rendered/occluded frames.
  Actual scan-out latency, live A/V lip sync and stable 60 fps from an Apple
  sender have not been measured.
- Tests used Windows 11 and GTX 1070. Physical Windows 10, AMD/Intel graphics,
  absent/removed GPU, audio-device replacement, network loss, sleep/resume,
  VPN/multiple-adapter/name-conflict cases and hours-long streams need coverage.
  Forced CPU/WARP presentation passes locally; device removal requires restart.
- PIN/password access control is not implemented. Use a trusted private LAN.
  There is no DRM bypass or claim of protected streaming-service support.
- Recording re-encodes frames and keeps its initial canvas through rotation.
  Compressed passthrough and filename templates are not implemented. Software
  HEVC encoding is not bundled; HEVC recording needs a supported hardware encoder.
  Fragmented MP4 improves interruption recovery but cannot guarantee recovery
  after every crash, disk-full event or abrupt power loss.
- Native toast notifications and Windows media-panel integration are absent.
  Volume/mute are session controls. Capture completion updates the viewer title.
- Updates require an Internet connection to GitHub only when explicitly checked
  or downloaded. Installed 0.1.0 must be upgraded once with the new installer;
  it did not contain an update checker. Portable folder replacement is manual.
- The application and installer are unsigned. Clean-user interactive installation,
  Start Menu/desktop shortcuts and uninstall need a separate account check;
  the automated installer tests use workspace extraction and file upgrades.
  A temporary uninstaller helper may remain in Windows Temp.
- Prior Debug/test firewall exceptions allowed Public-profile TCP/UDP. Earlier
  attempts to narrow those exact rules returned E_ACCESSDENIED. They remain
  unchanged and need administrator review. The app does not enable public access
  or router forwarding; see FIREWALL-REVIEW.md.

To finish device acceptance, connect an iPhone/iPad and verify discovery, video,
sound, portrait/landscape rotation, resizing/fullscreen, disconnect/reconnect,
PNG capture, MP4 playback, audio-device changes, Wi-Fi interruption, sleep/resume
and a long session while observing memory, handles and frame pacing.
