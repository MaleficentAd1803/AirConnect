# Known limitations and release gates

- No Apple device was available. Actual iOS/iPadOS Screen Mirroring discovery,
  encrypted media transfer, accompanying sound, rotation, reconnect, live
  recording, and long-running behavior remain **untested** in this fork.
- The Windows DNS service confirmed both advertisements locally. This does not
  prove that a remote device can see them through the current firewall/network.
- The automated desktop session reports `DXGI_STATUS_OCCLUDED`. GPU processing
  and actual swap-chain pixels were inspected, but visible physical-display
  presentation, scan-out pacing and interactive keyboard/mouse behavior need a
  normal desktop check. The Present statistic correctly remains zero when DXGI
  reports occlusion. UI image checks use the application's own rendering path.
- No AMD/Intel GPU or physical Windows 10 machine was tested. The application
  selects D3D11 generically and includes software decoding, but graphics-device
  removal currently reports an error requiring receiver restart. A forced
  WARP/Direct2D software presentation path passed locally without a hardware
  video processor; this does not replace testing on a physically GPU-less PC.
- Mirrored audio/video lip synchronization, network loss, sleep/resume, adapter
  changes, discovery name conflicts, default audio-device replacement, high-DPI
  monitor transitions and hours-long sessions need hardware coverage. Audio-only
  clock/FLUSH tests pass; that is not a measured live A/V-sync result.
- Automatic sender reconnection is not guaranteed. Discovery is retried after
  address/name changes and resume. The selected upstream interface strategy may
  need additional work on VPNs and unusual multi-adapter networks.
- PIN/password access control is not implemented. Use a trusted private LAN.
  Public-network firewall access and router port forwarding are not configured.
- Existing Debug/test executable firewall rules were discovered allowing TCP/UDP
  on the Public profile. Narrowing these exact task rules was denied by Windows
  (E_ACCESSDENIED). Administrator review is required; no other rules were touched.
- Recording re-encodes decoded frames; compressed passthrough and configurable
  filename templates are not implemented. HEVC software encoding depends on an
  available libx265 build. Output selection reports encoder failures. Fragmented
  MP4 limits damage after interruption but cannot promise lossless crash recovery.
- No Windows media-panel integration, album-art interface or HLS/DRM playback is
  advertised. There is no DRM bypass.
- Native toast notifications are not implemented yet. Capture completion updates
  the viewer title; errors and diagnostics remain inside the app. Playback
  volume/mute are session controls and are not restored after restart.
- The installer and executables are unsigned. Native per-user setup/uninstall
  source is included. Full interactive installation/Start Menu registration and
  uninstall in a clean Windows account need a separate check. The package test
  extracts and launches inside the workspace without changing user registration.
- A temporary uninstaller helper can remain in Windows Temp after removal.
- Dependency DLLs came from the pre-existing OBS/Git installations. Their hashes,
  versions and links are recorded, but exact corresponding dependency sources,
  build scripts and redistribution notices need review before **public** release.
  The local validation installer/ZIP should not be presented as a legally audited
  public distribution. The old OpenSSL declaration-header compatibility setup
  should be replaced by a matching OpenSSL 3 development package for release.

To finish device acceptance: connect an iPhone/iPad, verify discovery, pair,
display and sound, rotate, resize/fullscreen, disconnect/reconnect, record and
play back the MP4, capture PNG, change output device, interrupt/reconnect Wi-Fi,
sleep/resume, then run a long session and monitor handles/memory/frame pacing.
