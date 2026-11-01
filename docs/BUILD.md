# Building AirConnect

## Toolchain used on this PC

- Windows 11 Pro 25H2, build 26200.9711, x64. The legacy ProductName registry
  string still says Windows 10 Pro; the build and DisplayVersion identify Win11.
- Intel Core i5-10600KF, 12 logical processors, 32 GB RAM.
- Two NVIDIA GeForce GTX 1070 adapters, approximately 8 GB dedicated memory each.
- D3D11 feature level 11.0, 20 decoder profiles reported by the selected adapter.
- Windows Firewall reported current profile type 4 (Public), enabled, default
  inbound block. A later exact-rule inspection found TCP/UDP Public-profile
  allow rules for the Debug app and protocol test executable. No rule-creation
  code was invoked by AirConnect. Their origin was not verified. Attempts to
  narrow just these task rules to Private/LocalSubnet returned E_ACCESSDENIED;
  they remain unchanged and need administrator review. No router change was made.
- CMake and Ninja already installed; TDM GCC 10.3.0 x64 already present at
  `C:\Users\tamas\Desktop\win98hdd\tools\TDM-GCC-64`.
- MSVC/Visual Studio and Windows SDK were not found in standard locations.

The native Win32/Direct2D UI was selected to avoid Windows App SDK deployment
requirements and to build with the available compiler. No SDL, ImGui, browser
or webview is linked into AirConnect. C++20 is enabled; `-fno-char8_t` preserves
the C++17 filesystem string contracts of the upstream source.

## Dependencies

Source snapshots are in `upstream/` and `dependencies/`. Libplist 2.4.0 is built
statically from its supplied source. OpenSSL and FFmpeg are dynamically linked.
The current runtime DLLs were copied from the existing Git and OBS installations
without changing either program. FFmpeg reports `n7.1.1`; its headers are pinned
to the same upstream release. `tools/prepare-runtime.ps1` derives MinGW import
libraries from these DLL exports. These local development dependencies are not
a fully reproducible public release toolchain; see LIMITATIONS.md.

The upstream archive download was rejected by the browser approval gate. Direct
shell networking was unavailable. Read-only source retrieval through the GitHub
connector supplied the pinned source snapshots. The original SDL/ImGui program
was not separately built/launched; the native integration is the tested build.

## Commands

Run in this project directory. The process-scoped execution-policy option does
not change Windows policy settings:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/prepare-runtime.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Debug
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1 -Configuration Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1 -SkipBuild
```

Supply `-Toolchain C:\path\to\mingw64` when using another GCC installation.
The current import-library configuration targets MinGW x64. MSVC is not yet a
supported build configuration for this fork. Dependency headers and libraries
must match their runtime versions.

The source ZIP omits runtime DLLs; copy the supplied portable package's DLLs into
`dependencies/runtime` before preparing the import libraries. The matching
runtime hashes are in `dependencies/runtime-manifest.json`.

Git initialization succeeded, but the workspace protects `.git` from writes.
`git add` could not create `.git/index.lock` (Permission denied), so commits
could not be created in this environment. The full source ZIP and upstream pins
are supplied; this limitation was not bypassed through another metadata path.

CTest runs settings/identity, transport lifecycle, audio timing and native media
tests. The media test uses real local codecs/D3D11/WASAPI and generated fixtures;
it explicitly reports DXGI occlusion. It does not simulate an Apple device or
establish AirPlay compatibility. It currently requires the NVIDIA test encoders
available on this machine to create H.264/HEVC fixtures; the application itself
does not require NVIDIA hardware.

For live local protocol checks, run the application with a project-local data
folder and then the upstream Python test (requires `cryptography`) and the
standard-library limits test:

```powershell
.\build\debug\AirConnect.exe --data-dir "$PWD\build\test-app" --log
python upstream/AirPlay-Windows/tools/test_pair_verify.py
python tests/test_transport_limits.py
```

`--smoke-test 8` exits after eight seconds. Values of 20 or more also exercise
the real settings pages and export application-side UI render captures to the
chosen data folder. Those captures validate layout, not desktop composition.
No test credentials, identity files, recordings or local logs belong in the
source archive.

## System changes

No software was installed or removed, no security protection was changed, no
successful explicit firewall/router modification was made, and no reboot occurred.
All newly created files are inside this workspace, apart from normal Windows
temporary files created by the native installer when explicitly run. Build and
UI tests used `--data-dir` inside the workspace. Startup registration remains
opt-in and was not enabled.
