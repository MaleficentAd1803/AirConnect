# Validation record — 2026-11-01

This record distinguishes implementation from evidence. An implemented feature
is not automatically a tested Apple-device feature. There was no iPhone/iPad.

| Requirement | Implementation | Evidence / remaining check |
|---|---|---|
| Native executable / no web frontend | Implemented | Debug build and startup/exit passed; Win32/Direct2D/DWrite, D3D11, WASAPI |
| Discovery | Implemented | Native DNS-SD register/deregister callbacks returned status 0 for AirPlay and RAOP; remote device untested |
| Persistent receiver name / identity | Implemented | Unicode preference round-trip and stable DPAPI identity tests passed; live sender cache untested |
| Pairing / authentication | Implemented from upstream | 26/26 local protocol checks passed, including bad signatures and concurrent session isolation; Apple negotiation untested |
| PIN security | Not implemented | Upstream Windows foundation lacks the workflow |
| H.264 and HEVC | Implemented | Real generated codec fixtures decoded in CPU and D3D11 modes, including codec changes |
| Native video / aspect / orientation | Implemented | GPU surface captures show matching CPU/GPU output and letterboxing; portrait/landscape fixture changes passed |
| Visible display / frame pacing | Untested on physical display | Automated desktop returns DXGI_STATUS_OCCLUDED; no visible-presentation claim |
| Hardware / software fallback | Implemented | Explicit CPU and D3D11 decoder modes tested on GTX 1070; absent/removed GPU cases need coverage |
| Audio playback | Implemented | WASAPI initialized at 44.1 kHz, consumed silent PCM, flushed and stopped; audible live AirPlay audio untested |
| Audio timing | Implemented from upstream | RTP rollover, NTP mapping, future scheduling, pause bridge, FLUSH, immediate mirror mode and bounded shutdown tests passed |
| Audio/video sync | Partly implemented | Existing audio-only clocking retained; live mirror lip sync unmeasured |
| Fullscreen / PiP | Implemented | Native state transitions exercised; user interaction/monitor behavior pending |
| PNG screenshot | Implemented | GPU-decoded native-resolution PNG written and inspected |
| MP4 and audio recording | Implemented | GPU-resident H.264 MP4 encoded with zero CPU pixel readbacks; decoded pictures, audio stream, canvas and monotonic timestamps checked |
| Interrupted recording | Implemented as fragmented MP4 | Container strategy changed; crash/disk-full acceptance still pending |
| Settings / themes | Implemented | Persistent preferences tested; real settings pages and dark/light UI render captures inspected |
| DPI / keyboard navigation | Implemented basics | Per-monitor-v2 manifest and native keyboard controls; physical multi-DPI/interactive navigation untested |
| Tray / startup | Implemented | Tray lifecycle exercised by app startup/exit; startup registration left off and not changed during tests |
| Native toast notifications | Not implemented | Errors are shown in the application; capture completion currently updates the viewer title |
| Diagnostics / statistics | Implemented | Real counters/queue/bytes/local-time measurements; no invented CPU/GPU utilization |
| Parser / allocation bounds | Implemented | Local length/cap tests are recorded in the final test logs |
| Repeated connection / shutdown | Implemented | Five lifecycle rounds, EOF/reset, reconnect, partial requests, blocked sends, 200 short connections without growing completed-thread handles passed |
| Network changes / sleep | Implemented retry hooks | Physical interruptions, VPNs, name conflicts and sleep/resume untested |
| Installer / portable | Implemented | Packaging/extraction results recorded after final build; full clean-user interactive installation untested |
| Windows 10 / other GPUs | Intended support | Not physically tested |
| Source and component notices | Implemented | Pinned minimal FFmpeg/x264 source, matching OpenSSL 3 headers, OpenSSL source/Windows patches and notices accompany the release |
| Code signing | Unavailable | No signing certificate |
| Viewer menu / phone-shaped window | Implemented and locally tested | One native owner-drawn menu; portrait/landscape client aspect checks, fullscreen/PiP restore and idle visibility checks pass |
| Window placement | Implemented and locally tested | Serialized normal position/size round-trip and monitor-bound restoration helper tested |
| Installed updates | Implemented | Strict version/URL/hash/size/stable-release policy tests pass; live release download and native installer checks recorded below |

Both Debug and Release CTest passed all four suites. Release took 39.70 seconds:
native-core (0.07 s), transport-lifecycle (14.04 s), audio-clock (10.81 s), and
native-media (14.76 s). The staged AMOLED Release application exited normally
after a 24-second run, passed all 26 protocol checks and all nine input-bound/
connection-cap checks. Native registration and withdrawal returned status 0.

The media suite adds actual codec, D3D11, WASAPI, recording and capture checks.
CPU/GPU presentation-surface PNG comparison differed by at most 2/255 per channel
on the generated fixture. The forced WARP/Direct2D presentation fallback also
passed. GPU recording used NVENC H.264 with zero CPU pixel readbacks. Its MP4 was
decoded to verify pictures, the audio stream, fixed canvas and monotonic PTS.

Generated media
fixtures are isolated to the test executable; the shipped app contains no fake
sender or simulated video mode. Release results and artifact hashes are saved
beside the built packages.

Native installer compilation and cabinet extraction passed. The extracted
AirConnect.exe SHA-256 matched the tested Release executable exactly:
`d8f871fcf77ef482610091f3fe4a8f649b3920a27291d3b4fbf223a3d8b0375d`.
The test did not create Start Menu shortcuts, register an uninstaller or alter
startup settings. Those interactive installation paths remain untested.

## 0.2.0 verification

The minimal media runtime initially exposed missing ALAC test encoding and
D3D11VA2 options. The build options were corrected; the tests were retained.
Final Debug and Release builds each passed all five CTest suites: native-core,
transport-lifecycle, audio-clock, native-media and update-policy. The media suite
was repeated after the final viewer changes and passed in both configurations.
It covers CPU/D3D11 H.264 and HEVC changes, MP4 with audio and monotonic timestamps,
PNG output, actual portrait/landscape client aspect, one-menu control layout,
fullscreen/PiP restoration, idle hide/reveal, and forced WARP.

The idle test hides its own fixture window and checks the child's actual visible
style, preventing physical desktop activity from resetting its idle timer. It
still exercises the application's real timer and pointer-activity handlers.
D3D11 presentation tests have returned both S_OK and occlusion across runs;
S_OK is accepted presentation, not a measured physical scan-out guarantee.

App-generated AMOLED About captures were inspected: update status and controls
fit without clipping. The native automation pipe remains unavailable.

Installer transaction, staged startup/protocol tests and published GitHub asset
verification are recorded after release assembly.

The native installer transaction test passed: old 0.1.0 extraction, a deliberately
locked DLL causing replacement failure, restoration of the original executable
and libraries, a successful upgrade, removal of an obsolete owned x264 DLL, and
preservation of a personal sentinel file. It did not write uninstall registration
or alter the installed application. EXE and Setup version resources both report
0.2.0. The pinned runtime archive and each of its six DLLs passed SHA-256 checks.

The packaged receiver launched and exited with code 0. Both AirPlay and RAOP
registration/withdrawal callbacks returned status 0, and nine control-size,
connection-cap and recovery checks passed. The protocol script required an
additional project-local Python test dependency before its final rerun.

The packaged 0.2.0 executable then passed all 26/26 local pair-setup/pair-verify,
FairPlay framing and RTSP handshake checks with cryptography 50.0.2 in a
project-local validation environment. All nine malformed-input/connection-cap
checks passed again, and the process exited with code 0. The final Release CTest
run passed all five suites in 42.91 seconds; Debug passed all five suites and the
latest media changes passed its repeated 18-second native-media run.

Public asset and native updater download checks are recorded in
[the post-publication verification](RELEASE-VERIFICATION-0.2.0.md).
