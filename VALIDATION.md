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

The final DLL starts with Menu=0, Metadata=0 and Log=0, and Log=0 leaves the
existing log size and modification time unchanged. Review approved the bounded
publication/log/settings workers, reference forwarding and the descriptor-only
cache collector correction. An isolated rapid restart exited with 0xC000001D;
a subsequent debugger launch survived without reproducing the exception.
The release-checkout artifact also survived debugger startup with all optional
integrations enabled and saved a complete **24-bit** track. Full
independent decoding verified zero errors, exact PCM sample count, embedded
artwork/tags and the original STREAMINFO audio MD5. The activity log recorded
started and finished entries.

Released version.dll SHA-256:
`2bad10c40c71394b52a8966a1b93f1305a209d2a7647c3d50dcbe9e18faa4a3b`.

Recording names, per-file hashes and personal listening data are not published.
