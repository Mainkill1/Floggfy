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

Previous v1.1.0-rc.1 `version.dll` SHA-256:
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

The archived Spotify 1.2.92.148, 1.2.94.583 and 1.3.0.277 packages were also
checked with their actual UI binaries. The production PE scanner found the
`cef_menu_model_create` delay import in every `Spotify.dll`, and every paired
`libcef.dll` exported that factory and loaded as CEF 146.0.10 commit 3504. The
installed Spotify 1.3.3.264 client uses CEF 151.3.18 commit 3578. Official headers
for both revisions have the same required 41-method menu prefix and the same seven
delegate callbacks. This establishes static discovery and ABI compatibility for
the archived packages; they were not executed end to end.

Recording names, per-file hashes and personal listening data are not published.

## CEF client identity crash repair

On Spotify 1.3.3.264, opening the mini player with metadata enabled reproduced
CEF's fatal `UnwrapDerived called with unexpected class type 0` check. Disabling
only metadata prevented that crash. The bridge now preserves the original CEF
client and handler objects and intercepts their callback function addresses.

The Windows identity regression failed before the repair and passed afterward.
It covers all three browser factories, unchanged client/handler structures,
original callback arguments and transferred browser references. The Linux
native/JavaScript suite and Windows history/log/metadata suite all passed.

With metadata enabled, Settings → Connected apps → View succeeded twice and
the mini player opened without a fatal debugger exception. The cached metadata
collector still received fields. These are checks of the reported UI paths;
they do not resolve earlier intermittent freeze reports.

Repaired `version.dll` SHA-256:
`59503908963397442c8daf4ae95407c98aacc7a0aac714c2866ee8346af82098`.

## Cached catalogue metadata enrichment

The renderer collector now exports every cached contributing artist and album
artist, retains full release dates, and separates label/organization tags from
explicit publisher credits. The metadata worker also reads existing Spotify
LevelDB tables and complete WAL records through shared read-only file handles;
it does not open/recover the database, take its lock, query endpoints or construct
client services. Exact cache URI, embedded protobuf GID, title, album and duration
are verified before merge. Cached artist lists cannot remove renderer artists.

Synthetic Linux and Windows tests passed for separate album records, signed
protobuf dates, artist lists, label/copyright tags, exact identity and wrong-GID
rejection, Snappy compression, WAL fragments/truncated tails and tombstones.
Checksum-valid repeated block handles and excessive aggregate expansion are
rejected. AddressSanitizer and UndefinedBehaviorSanitizer passed the cache suite.
The Windows suite also verified concurrent shared reads, an exclusively held
WAL, and rejection of readable corruption instead of silently using older data.
Existing history, Ogg/FLAC tagging, log and CEF object-identity regressions passed.

The installed Windows Spotify 1.3.3.264 client received enriched fields through
the production metadata worker, stayed responsive and initialized connectivity
repair. A metadata-only update to an existing FLAC preserved its compressed audio,
STREAMINFO and embedded artwork byte for byte. Private recordings and cache
records are not included in the repository or release.

Lookup work uses background I/O priority, bounded files/bytes, block expansion
limits and a 24-entry memo cache. Its two-second cutoff is cooperative between
file operations, not cancellation of a stalled disk read. Spotify can exclusively
hold its active WAL; in that specific sharing-lock case the reader uses matching
readable SST catalogue records, which may lag WAL updates or evictions. Other
read/parse failures retain ordinary tags. Missing fields remain absent. This
change does not automatically retag the entire existing library, and earlier
cross-version/CEF support limits still apply.

Enriched `version.dll` SHA-256:
`153b7d52f4f49973efaedceee79e17348c186158f7d1ce3dd27524448c2a66e0`.
