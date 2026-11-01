# Public release verification: 0.2.0

Release: https://github.com/MaleficentAd1803/AirConnect/releases/tag/v0.2.0

The tagged application source is commit `c3dd7ac`. This post-publication record
does not change the released binaries or their checksums.

- GitHub's SHA-256 digest matched every uploaded asset: installer, portable ZIP,
  application source, corresponding dependency sources, build runtime and sums.
- The native updater fetched the public latest-release metadata, accepted its
  version/asset policy, downloaded the real installer through WinHTTP and verified
  its SHA-256 digest with Windows CNG. The downloaded EXE also matched the local
  release EXE hash independently.
- The build bootstrap downloaded the public runtime archive anonymously and
  verified the archive and all six individual DLLs against committed hashes.
- Debug and Release passed all five native test suites. The final Release run
  took 42.91 seconds; final viewer media/idle changes also passed in Debug.
- The packaged receiver passed 26/26 pairing/protocol checks and 9/9 malformed
  input, connection-limit and recovery checks, and exited with code 0. AirPlay
  and RAOP registration and withdrawal callbacks returned status 0.
- The native installer passed old-version extraction, locked-file rollback,
  successful 0.1.0 upgrade, obsolete-file removal and personal-file preservation.
  Tests used a workspace fixture and did not alter the installed application.

No Apple device was available. Live iOS/iPadOS compatibility, audible mirrored
audio, real-device rotation/reconnect/recording, long sessions and physical
multi-DPI use remain unverified. The native UI automation pipe was unavailable;
app-generated visual captures and native test-window checks were used.

Development downloads, build helpers and the Python validation environment stayed
inside the project. Only this project's Git metadata ownership was corrected to
the current Windows user so ordinary Git commands work. No unrelated application,
startup setting, firewall rule, router setting or operating-system security
protection was changed, and the running installed copy was not stopped.
