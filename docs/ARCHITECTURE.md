# Implemented architecture

`Receiver` owns a cancellable service worker, upstream RTSP server, native DNS-SD
advertiser, DPAPI-backed Ed25519 identity, and shared live stream settings. The
home UI reads synchronized status snapshots. It does not run network or codec
operations on its message thread. Receiver states expose Idle, Discoverable,
Connecting, Connected, Mirroring, Audio-only, Disconnecting and Error. Connecting
tracks actual pair-setup/pair-verify activity; Connected follows negotiated SETUP.

`VideoRenderer` retains the upstream protocol-facing interface while owning a
separate native viewer/render thread. FFmpeg's decoder shares its D3D11 device.
D3D11 video processing converts/scales decoder textures into the swap chain.
The display queue has one pending AVFrame reference and overwrites obsolete
frames. Input views are cached and decoder surfaces are reference-counted.
Software decoding converts to BGRA then uploads a native D3D11 render texture.
If no usable video processor exists, the display uses WARP and Direct2D instead.
The GPU display path has no CPU pixel readback. Explicit screenshots and test
captures do perform readback. The D3D11 immediate context is multithread protected.

`SdlAudioOutput` keeps its historical name only at the protocol boundary; its
implementation is now WASAPI. A dedicated COM-initialized worker uses shared-mode
event-driven playback, Windows resampling, a 250 ms bounded PCM queue, sender gain
and independent local volume/mute. It reopens invalidated endpoints and checks
default-device changes. Upstream RTP/NTP scheduling is preserved for audio-only
streams; mirrored audio remains immediate. Live lip synchronization is unmeasured.

`VideoRecorder` uses a separate bounded worker queue, the upstream D3D11 encoder
conversion path and FFmpeg MP4/AAC muxing. GPU queues retain at most two decoder
surfaces; CPU queues retain at most six. Captures require an explicit action.
Fragmented MP4 improves interruption recovery, and recording names avoid
overwriting another recording created in the same second.

`Preferences` writes a UTF-8 INI through an atomic replacement in LocalAppData.
Receiver seeds are encrypted through current-user DPAPI. `Home` uses Direct2D/
DirectWrite with real native controls, owner-drawn category navigation, native
keyboard focus, tray integration and dark/light palettes. The viewer is a
separate native top-level window with fullscreen/PiP and auto-hiding controls.

Control bodies are capped at 8 MiB, headers at 64 KiB and concurrent control
connections at eight. Mirror payloads retain the upstream 2 MB bound. Malformed
and conflicting lengths are rejected before allocation. Logs suppress Debug
packet output and known key/seed material. No data is sent to a service.

Primary sources:
- https://github.com/moieric11/AirPlay-Windows/tree/f739810e35887cf572b69275a52dc6f32323d65d
- https://github.com/FDH2/UxPlay/tree/f40b121b6b932fb8ed138ce2e492b0a77cf284a3

The UxPlay reference uses GStreamer appsrc pipelines for audio/video and exposes
PIN/password policy hooks. AirPlay-Windows provides the closer native Windows
foundation, binary-plist negotiation, pair-verify, mirror transport, RAOP and
FFmpeg codecs. Its on-screen PIN workflow is absent; adding a decorative PIN
setting would misrepresent access control, so none was added. Experimental HLS
proxy playback, USB supervision and music artwork controls are not enabled in
the normal AirConnect frontend.

`UpdateService` runs explicit WinHTTP release checks and downloads off the UI
thread. It accepts only stable numeric versions and the exact repository's
installer URL, caps metadata/download sizes, and verifies GitHub's SHA-256 digest
with Windows CNG before exposing installation. The native installer waits for
clean process exit and stages replacements with rollback. Data directories remain
outside the installation manifest.

Window placement is stored atomically in separate home/viewer files, avoiding
cross-thread preference overwrites. Restoring adjusts size for DPI and clamps
off-screen positions to the nearest work area. Native menu/size modal loops use
a short-lived render timer to consume the same single pending frame slot.
