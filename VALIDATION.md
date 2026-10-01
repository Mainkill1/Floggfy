## Automated checks

`bash test-native.sh` covers Ogg integrity/complete listens, native artwork/tags,
FLAC metadata and contiguous frame coverage, fixed RAM block budgets, requested
layout and conservative quality rules, bounded UTF-8 metadata identity, and a
publication queue whose consumer is deliberately blocked. The collector test
uses simulated existing client caches for date, genre and Unicode lyric transport.
Request functions and property getters are traps: zero requests, service
resolutions or accessor invocations are allowed. Unrelated track caches are
ignored, and later cache updates are collected without fetching anything.

Windows tests exercise real file APIs: equal/higher-quality skips, failed-upgrade
preservation, atomic upgrades, downgrade prevention, asynchronous INI persistence,
no premature save-folder creation and the 5 MiB log reset/oversize/unwritable cases.
Debugger startup checks survive and detach. Actual menu clicks toggle Downloads,
FLAC and Ogg and persist their values; Menu=0 startup and the folder picker also
have been checked.

## Final release checks

The cache-only collector was loaded in the real client and located its existing
PlayerAPI without constructing services. A track was saved after a complete
listen through the background publication worker; independent full decoding
verified its source MD5 and embedded artwork. Its cached Spotify URI, disc number,
disc total and track total were embedded. Release date, genre and lyrics were
absent from the observed real playback cache and were omitted. Simulated cached
field tests establish transport correctness; they do not establish availability
of those fields in every live client. No Spotify endpoint requests are made.

The release candidate was separately started with Menu=0, Metadata=0 and Log=0;
Log=0 left the existing log size and modification time unchanged. Review covered the bounded
publication/log/settings workers, reference forwarding and the descriptor-only
cache collector correction. Eight one-second rapid restart runs exited normally,
and a debugger startup/detach check left the process running.

With all optional integrations enabled, the installed candidate saved a complete
native FLAC from Spotify 1.3.3.264. The official Xiph `flac` decoder reported zero
errors, verified the original STREAMINFO MD5, decoded 6,313,591 samples at 44.1 kHz
stereo, and found embedded front-cover art plus the required tags. The activity
log recorded one decoder initialization, one complete stream, one complete listen
and one saved file, with no decoded-frame coverage gap.

The production audio resolver was run against Windows-mapped Spotify DLLs from
1.3.3.264, 1.3.0.277, 1.2.94.583 and 1.2.92.148. All six targets were found at
the independently established function RVAs in every sample. The production
connectivity resolver found `CoCreateInstance` in every sample by import name,
without a Spotify hash, RVA or provider DLL assumption.

Current v1.1.0 release-candidate `version.dll` SHA-256:
`854249b471baeea8d7072d4403018fde56e933a6a7ddc5e372a4b6e19fd6cd67`.

An installed-client restart of this exact DLL produced two connectivity patch
events in the same startup, showing that the monitor restored the delay-IAT slot
after Windows resolved and replaced it. The Network List Manager hook then
installed, the compatibility override ran, Spotify kept established connections,
and the dynamic audio, metadata and menu hooks initialized. No connectivity-hook
failure was logged.

The menu integration was then changed from an exact CEF identity allowlist to
runtime capability discovery. Synthetic tests accept the audited structure size
and larger append-compatible structures, reject truncated structures or missing
methods, and recognize a localized top-level menu by item types rather than text.
The installed Spotify 1.3.3.264 client exposed a 488-byte model; the release DLL
validated its required executable methods, installed both menu hooks and inserted
Downloads, Save Location, FLAC and Ogg without consulting the CEF version.

Recording names, per-file hashes and personal listening data are not published.
