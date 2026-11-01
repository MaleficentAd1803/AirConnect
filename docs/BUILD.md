# Building AirConnect

Windows x64, C++20, CMake 3.21+, Ninja, and a MinGW-w64 toolchain containing
gcc, g++, windres and dlltool are required. Python 3.13+ is used for dependency
verification and source packaging. Windows supplies makecab for the installer.

The validated local toolchain is TDM GCC 10.3, CMake 4.4 and Ninja on Windows 11
25H2, with an i5-10600KF, 32 GB RAM and GTX 1070. MSVC is not the configured
build system. Normal execution requires none of these development tools.

## Build with the release's exact native libraries

Clone the repository, then run PowerShell in its root. `-Toolchain` is the folder
containing `bin\gcc.exe`. It can also be supplied through AIRCONNECT_TOOLCHAIN;
otherwise the existing CMake cache or PATH is used.

```powershell
python tools/fetch-runtime.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare-runtime.ps1 -Toolchain C:\tools\mingw64
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Debug -Toolchain C:\tools\mingw64
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release -Toolchain C:\tools\mingw64
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1 -SkipBuild -Toolchain C:\tools\mingw64
```

The process-scoped execution-policy option does not change system policy.
Runtime archives and individual DLLs are checked against committed SHA-256
values. Import libraries are derived from the exact DLL exports. Libplist is
built from the included source. C++20 uses `-fno-char8_t` to preserve the upstream
protocol's original filesystem string contracts. The app and installer link the
compiler runtime statically; no Visual C++ runtime installer is needed.

## Rebuild the media libraries

Install Git for Windows and NASM in addition to the tools above. Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare-media-build.ps1 -Toolchain C:\tools\mingw64
```

Source archives and the project-local GNU make/pkg-config tools are downloaded
from their pinned official sources, verified, and kept under `build/`. Git Bash
provides the POSIX shell and utilities. No MSYS installation or global PATH
change is performed. The build produces `build/media-sdk/bin/*.dll` and matching
headers. Use a fresh media-sdk/media-src directory when changing a dependency
pin or build options; an existing x264 library is deliberately reused.

FFmpeg 7.1.1 is built with x264, AAC/ALAC, H.264/HEVC, D3D11VA and D3D11VA2,
NVENC, MP4, swscale and swresample. OpenSSL 3.5.5 uses the exact Git for Windows
SDK binary and matching headers described in THIRD_PARTY_NOTICES. Its complete
source and Windows build recipe/patches are in the dependency source asset.
Byte-for-byte reproduction across compiler versions is not promised.

## Tests and releases

CTest covers preferences/identity/window placement, transport lifecycle, audio
timing, native media and update policy. Native media tests require the NVIDIA
fixture encoders available on the validation PC; the app itself selects D3D11
generically and supports CPU decoding/WARP. A machine without that test hardware
cannot provide the same hardware coverage. Compilation or generated fixtures do
not establish Apple-device compatibility.

Update `src/version.h`, test the change, update docs/RELEASE.md, then use
`tools/release.ps1` to build and package. With `-Publish`, that script requires
committed source, tags and pushes it, and publishes installer, portable, source,
dependency sources and checksums through an authenticated GitHub CLI. Publication
is opt-in; building does not publish or change installed software.

The original inspection used pre-existing OBS/Git DLLs while shell networking
was restricted. The release replaced OBS with the pinned project-built minimal
runtime and matching OpenSSL 3 headers. No unrelated application was modified,
no development tool was uninstalled, and no reboot was performed.

The optional Python pairing tests use `tests/requirements.txt`. Install it in a
project-local virtual environment; it is never required by the receiver.
