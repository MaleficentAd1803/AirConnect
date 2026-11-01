AirConnect 0.2.0 adds a single AMOLED viewer menu, a window that follows the
phone's aspect ratio and rotation, and persistent main/viewer window placement.
Menus and live resizing continue consuming the bounded video frame slot.

Settings → About can check GitHub releases, download a SHA-256-verified installer
and install it after clean receiver shutdown. The installer supports upgrades,
restores previous files if replacement fails, removes obsolete owned files and
preserves settings, identity, recordings and unrelated files.

The package uses a small project-built FFmpeg/x264 runtime and matching OpenSSL 3
headers. Installer, portable ZIP, application source, corresponding dependency
sources, a pinned build-runtime archive and SHA-256 sums are included.

For installed 0.1.0, exit AirConnect from its tray menu and run this installer once.
Later installed versions can use the in-app update controls. Portable users
should extract the new ZIP after closing the old copy.

This is an unsigned development release. No Apple device was available for final
end-to-end acceptance. Local protocol, codec, native graphics/audio and installer
tests are described in VALIDATION.md; they do not prove live iOS compatibility.
