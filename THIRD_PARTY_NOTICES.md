# Third-Party Notices

## AirConnect fork and actual bundled runtimes

AirConnect's new native UI, WASAPI backend, lifecycle, security bounds and
packaging changes are GPL-3.0. The pinned AirPlay-Windows source is retained in
`upstream/AirPlay-Windows`; source provenance and local DLL hashes are in
`dependencies/runtime-manifest.json` in the companion source archive.

The normal AirConnect executable does **not** link SDL2, SDL_ttf, Dear ImGui or
GStreamer. Entries below describing those components apply to the preserved
upstream project/reference material.

The actual local FFmpeg DLLs report **GPL version 3 or later**, not LGPL. They
come from the existing OBS dependency build of FFmpeg n7.1.1, configured with
`--enable-version3 --enable-gpl --enable-libx264` and other OBS codecs. This
supersedes the upstream FFmpeg license description below for the supplied
validation package. Exact configuration and hashes are in the runtime manifest.

OpenSSL reports 3.5.5 and is from the existing Git installation (Apache-2.0).
The compatibility declaration headers currently originate in OpenSSL 1.1.1w;
their older OpenSSL/SSLeay notice is included separately. They use opaque handles
and are validated against the runtime by identity/pair-verify tests. Replace
them with a matching OpenSSL 3 development package before public release.

Libplist 2.4.0 is compiled from the supplied source under LGPL-2.1-or-later;
libcnary's notice is retained in its source. Native tests also reuse fixture
helpers from the upstream GPL-3.0 GPU tests. The native icon is original geometry.

Additional runtime components include x264 (GPL), zlib (zlib license), librist
(BSD), SRT (MPL-2.0) and the Microsoft Visual C++ runtime DLLs. FFmpeg also embeds
its OBS-configured codec dependencies. Those exact matching sources, build
recipes, Microsoft redistribution terms and complete component notices have
**not** yet been assembled/audited for public redistribution. The installer and
portable archive are local validation artifacts, not a completed public release.
Do not omit that limitation or treat this document as a redistribution approval.

Relevant source projects:
- https://github.com/moieric11/AirPlay-Windows
- https://github.com/FDH2/UxPlay
- https://github.com/obsproject/obs-deps
- https://github.com/FFmpeg/FFmpeg/tree/n7.1.1
- https://github.com/openssl/openssl/tree/openssl-3.5.5
- https://github.com/libimobiledevice/libplist/tree/2.4.0
- https://code.videolan.org/videolan/x264
- https://github.com/Haivision/srt
- https://code.videolan.org/rist/librist
- https://zlib.net/

The upstream notices follow, unchanged apart from this clarification.

This project ships, links against, or fetches at build time the
components listed below. Each entry states the upstream source, the
license under which it is redistributed in this repo, and any caveat
specific to the use we make of it.

The project itself (everything under `src/`, `tools/`, this file, the
`CMakeLists.txt` and the `README.md`) is released under the
**GNU General Public License v3.0** — see [`LICENSE`](LICENSE).

---

## 1. Bundled in this repository

### `third_party/playfair/`
- **Origin**: [github.com/EstebanKubata/playfair](https://github.com/EstebanKubata/playfair),
  also bundled under `lib/playfair/` in [UxPlay](https://github.com/FDH2/UxPlay).
- **License**: GNU General Public License v3.0 (file
  `third_party/playfair/LICENSE.md`).
- **What it does**: implements the obfuscated FairPlay decryption
  routine that converts the 72-byte `rsaaeskey` from `ANNOUNCE` into
  the AES-128 key used to decrypt the audio / mirror payload.
- **Modifications**: none — the C sources are vendored as-is.

### `third_party/fairplay_blobs_real.cpp`
- **Origin**: derived from `lib/fairplay_playfair.c` in
  [UxPlay](https://github.com/FDH2/UxPlay) (GPL-3.0). The byte
  sequences themselves were originally captured from an Apple TV
  responding to `POST /fp-setup` requests.
- **Our wrapping**: we re-emit the same 4×142 byte `reply_message`
  table and 12-byte `fp_header` constant in a dedicated translation
  unit that exposes them through the `ap::crypto::fairplay_blobs`
  API.
- **License of the C++ wrapper**: GPL-3.0, as a derivative of UxPlay.
- **License of the underlying byte sequences**: **none granted**.
  The bytes are produced by Apple's FairPlay implementation and are
  Apple's copyrighted output. We redistribute them on the same basis
  the parent projects (UxPlay, RPiPlay, shairport-sync) do — they
  have been published this way for several years without enforcement
  action — but **we make no claim of ownership** and we do **not**
  grant any rights over them. If Apple Inc. requests removal we will
  comply.
- **Why bundled**: without these 568 bytes, iOS aborts the AirPlay
  handshake at the FairPlay step. Bundling them allows the receiver
  to work after a plain `git clone && cmake --build`. Anyone with
  access to a real Apple TV on the same network can recover the same
  bytes by capturing its `/fp-setup` traffic; see
  [`docs/FAIRPLAY.md`](docs/FAIRPLAY.md) for the procedure.

---

## 2. Linked at build time (vcpkg / system packages)

### libplist
- **Upstream**: [github.com/libimobiledevice/libplist](https://github.com/libimobiledevice/libplist)
- **License**: GNU Lesser General Public License v2.1 or later.
- **Use**: parse / build Apple binary plists in `/info`,
  `ANNOUNCE`, `SETUP`, etc.

### OpenSSL
- **Upstream**: [openssl.org](https://www.openssl.org/)
- **License**: Apache License 2.0 (since OpenSSL 3.0).
- **Use**: Ed25519 receiver identity, X25519 ECDH, AES-128-CTR,
  SHA-512 for the AirPlay pair-setup / pair-verify handshake.

### FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- **Upstream**: [ffmpeg.org](https://ffmpeg.org/)
- **License**: LGPL-2.1+ (the `--enable-gpl` codecs are not used
  by this project; only LGPL-licensed components are required for
  the H.264 / HEVC / AAC decoders we depend on).
- **Use**: decode H.264 + HEVC mirror streams, decode AAC-ELD audio.

### SDL2
- **Upstream**: [libsdl.org](https://www.libsdl.org/)
- **License**: zlib license.
- **Use**: window, GPU YUV→RGB texture upload, audio output, event
  loop, hit-test for the borderless "Hide UI" mode.

### SDL2\_ttf
- **Upstream**: [libsdl.org/projects/SDL_ttf](https://www.libsdl.org/projects/SDL_ttf/)
- **License**: zlib license.
- **Use**: text rendering for the metadata overlay (cover art / track
  title / progress).

### GStreamer (optional)
- **Upstream**: [gstreamer.freedesktop.org](https://gstreamer.freedesktop.org/)
- **License**: LGPL-2.1+.
- **Use**: opt-in HLS playback backend for the AirPlay Streaming
  proxy path (`--hls-proxy-playback`). Falls back to libavformat if
  not present at build time.

---

## 3. Fetched at configure time

### Dear ImGui
- **Upstream**: [github.com/ocornut/imgui](https://github.com/ocornut/imgui)
- **Pinned tag**: `v1.91.5` (see `CMakeLists.txt`).
- **License**: MIT License.
- **Use**: in-app overlay rendering (toolbar, options panel, status
  bar, sidebar, dialogs).

### Avahi (Linux dev builds only)
- **Upstream**: [avahi.org](https://www.avahi.org/)
- **License**: LGPL-2.1+.
- **Use**: mDNS advertising on Linux (`HAVE_AVAHI` build path). The
  Windows release uses the OS-native `dnsapi.dll` and never links
  Avahi.

---

## 4. Reverse-engineered protocol references

This project would not exist without prior open-source AirPlay
reverse-engineering. None of the sources below are copied verbatim,
but their structure and field names are echoed throughout `src/`:

- [UxPlay](https://github.com/FDH2/UxPlay) (GPL-3.0) — primary
  reference for the AirPlay 2 RTSP / mirror / audio pipeline.
- [RPiPlay](https://github.com/FD-/RPiPlay) (GPL-3.0) — UxPlay's
  ancestor; the pair-setup / pair-verify and FairPlay handshake
  framing was first documented there.
- [shairport-sync](https://github.com/mikebrady/shairport-sync)
  (MIT) — RAOP audio packet format reference.
- [openairplay/airplay-spec](https://openairplay.github.io/airplay-spec/)
  (CC-BY-SA-4.0) — community notes on the AirPlay 1/2 protocol.

---

## 5. How to ask us to remove something

If you hold rights to any of the components above and disagree with
the way we redistribute them, please open an issue at
<https://github.com/moieric11/AirPlay-Windows/issues> — we will
respond within a reasonable delay and remove or replace the asset
as appropriate.
