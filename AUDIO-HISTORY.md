# To Disk audio history

The native `version.dll` combines the connectivity repair with complete-listen
history for the Windows x64 Spotify client. It copies the compressed
Vorbis or FLAC bytes that Spotify already reads for playback. It does not request
tracks, run FFmpeg, transcode audio, or require an external runtime helper.

## Settings

`SpotifyHistory.ini` is included with the release and is created beside
`version.dll` on first launch when missing.
The top-left menu contains **To Disk** with Downloads, Save Location, FLAC and Ogg.
Checkbox labels and capture switches change immediately when clicked. INI
persistence runs in a background worker.

```ini
[To Disk]
Downloads=1
Menu=1
Save Location=
Flac=1
Ogg=1
Metadata=1
Lyrics=1
Log=1
DebugLog=0

[History]
MaxBufferedMiB=500
```

- **Downloads:** master switch. Start the next track from its beginning after
  enabling capture; enabling halfway through a track cannot recover earlier audio.
- **Menu:** set `0` and restart Spotify to disable menu integration. The connectivity
  repair and configured capture continue to operate without this menu hook.
- **Save Location:** empty defaults to the Windows Music known folder's `Spotify`
  subfolder, including redirected Music folders. A custom directory is accepted.
- **FLAC/Ogg:** select which original source formats to retain. Enabling FLAC does
  not convert Vorbis playback into lossless audio. Spotify must supply FLAC.
- **Metadata:** enriches tracks using existing client state, cached metadata, and Spotify's authenticated track/album services. Set 0 and restart to disable its hooks.
- **Lyrics:** default on. Requests available lyrics automatically through Spotify's
  existing client service, even with the lyrics panel closed. Set 0 in the INI or
  use the menu toggle to stop lyric discovery, cancel pending lyric work, and
  disable lyric enrichment while retaining other metadata and artwork.
- **Log:** default on; started/failed/finished activity in the save root, capped at 5 MiB.
- **DebugLog:** default off; opt-in diagnostic detail in the same bounded log.
- **MaxBufferedMiB:** 8–512 MiB, default 500, limits compressed capture allocations.
  There is also an approximately 8 MiB event queue, artwork, and a temporary
  completed-file buffer during native tagging. Oversized tracks are discarded.

## Files and duplicates

Default music paths are:

```text
<Windows Music>\Spotify\Artists\Artist\Album\01 - Title.ogg
<Windows Music>\Spotify\Artists\Artist\Album\01 - Title.flac
```

Album folders have no year prefix. Filenames have no timestamps. Missing track
numbers omit the numeric prefix. Later discs include a disc prefix. Title, artist,
album, album artist and track number are embedded in the single audio file.
Validated cover artwork is included when available; missing artwork does not
discard completed audio. No image/JSON
sidecars or per-song directories are created. Buffers remain in RAM until a
complete listen and complete stream have been verified. Directories are created
only when publishing a file.

Existing audio is skipped at equal or higher quality only after validating its
complete container and recording identity. Spotify track URI takes precedence;
without it, the full textual identity, disc/track numbers and duration must agree.
Sanitized or shortened names that collide use a deterministic suffix. Unrelated
or unverified files are preserved. A verified complete FLAC suppresses a Vorbis
counterpart only for the same recording.

A Vorbis upgrade requires higher known nominal bitrate and no sample-rate
reduction. FLAC upgrades require matching channel count, no bit-depth/sample-rate
reduction, and a verified increase. All saves first write and flush a completed
file beside the destination, then promote it without overwriting an unexpected
target. Upgrades reserve the verified old file before promotion; an interruption
can briefly leave the usual path absent, but keeps the old recording recoverable.
The next publication attempt recovers the retained recording without overwriting
a conflicting target. No track fragments are staged during playback.

Choosing a custom `Media` root retains the requested `Music\Artists` branch.
The default Music/Spotify root uses `Artists` directly to avoid another Music
folder. The path builder also supports Compilations, Soundtracks, Various Artists,
Audiobooks/Author/Series/Book, Podcasts/Show/Year, Music Videos and Concerts. These
folders are never precreated. Live Windows media metadata currently identifies
music and Various Artists; reliable category, book-series and podcast-year
metadata enrichment remains additional work. Video extraction is not implemented.

## Verification and limits

FLAC uses the original decoder's compressed-input callback and validates contiguous
decoded-frame sample coverage. Native metadata rewriting preserves STREAMINFO and
all compressed frame bytes, adds VORBIS_COMMENT and PICTURE blocks, and needs no
encoder. See `FLAC-SUPPORT.md` for the hook boundaries and FLAC verification.
Nine actual complete FLAC listens also passed full independent decoding and
original audio MD5 verification, with embedded artwork and metadata.
Personal recording details are not published.

Skips, seeks, missing pages/frames, decoder errors, ambiguous track association,
queue overflow and memory limits prevent publication. Capture begins with source
headers and a listen observed near position zero. Streams without a known FLAC
total sample count are currently rejected. Natural same-track repeats establish
a new listen generation after a bounded timeline/title confirmation interval;
seeks, pauses and excessive observation gaps do not count as completed listens. Metadata fields absent from the public Windows session
are not invented. Audio targets are discovered from invariant decoder instructions
and Windows x64 function metadata; missing or ambiguous matches disable capture.
Menu integration checks its supported CEF version and structure sizes.

Activity and opt-in diagnostics are in `Floggfy.log` in the selected save root.
At 5 MiB, the log removes the oldest complete lines to make room for each new
entry. Logging and INI persistence use separate workers.
Completed saves log the captured audio format/quality and final tagged file size
in MiB and bytes. Upgrade, metadata-only update, skipped-file, changed-setting and
failure outcomes are distinct. Equal/better existing audio is identified only
when the on-disk format is verified; incomparable or unverified files get an
explicit retention reason.

The last ten successful saved paths are held in memory for this Spotify run.
**Latest Downloads**, directly below **Save Location**, is hidden until the first
successful download. Selecting a song opens its containing folder in Explorer.
Skips, failed writes and metadata-only updates do not add history entries. Menu
snapshots stay fixed while open so a new download cannot redirect an existing
item's click target. A deleted file or folder produces an unavailable notice;
Explorer failures produce an error notice. Menu construction performs no filesystem
or shell work.

The current **Song** row stays visible but greyed out unless a matching saved
FLAC or Ogg file exists. When enabled, it opens the containing folder. A separate
worker checks destination paths; capture processing and menu rendering only
exchange bounded requests and memory snapshots. Superseded results cannot enable
a different track or save location, and `Menu=0` disables destination probes.
The click rechecks the file so deletion produces an unavailable notice instead of opening an
empty folder. Only audio saves create directories. Folder clicks do not add
download history entries.

Tagging, quality checks and publication run in a bounded background worker;
pending compressed save buffers count against the configured allocation budget.
Existing-file inspection uses checked reads with a separate temporary buffer
capped at 512 MiB; storage errors reject the inspection without stalling capture.

## Troubleshooting

1. Quit Spotify and set **Metadata=0**, then restart. This isolates client metadata
   service/cache discovery changes while retaining basic Windows media tags and artwork.
2. If menu integration crashes or disappears, set **Menu=0** and restart. Configure
   Downloads, FLAC and Ogg directly in the INI.
3. Enable **Log=1** and, temporarily, **DebugLog=1**. Inspect `Floggfy.log` in the
   configured save root. Turn debug logging off afterward.
4. Confirm Downloads is on and the actual source format is enabled. Lossless
   playback produces FLAC; its Ogg parser is unused. Enabling FLAC cannot turn a
   lossy Spotify stream into lossless audio.
5. Start a fresh track at position zero and let it finish. Seeking, skipping,
   truncated input, decoder errors and ambiguous association prevent saving.
6. For unusually large lossless tracks, increase MaxBufferedMiB within the
   512 MiB limit.
   Check the destination is writable and has space; try a local drive.
7. After a Spotify update, check the log for missing or ambiguous audio targets.
   The dynamic resolver supports shifted functions but safely rejects rewritten
   boundaries.
   Include your Spotify version and a short redacted failure excerpt in an issue.
8. To uninstall, quit Spotify and remove `version.dll`. Remove the INI if you no
   longer need its settings.


## Development checks

Requires MinGW-w64, Python 3, a native C/C++ compiler and Node.js for the tests.

```bash
bash test-native.sh
bash build-native.sh
# Actual Windows file/settings/log tests, from WSL:
bash test-native-windows.sh
python3 package-release.py
```

`tests/windows_history_test.cpp` exercises actual Windows file publication,
duplicate skips, failed upgrades, staged publication and preference persistence.
Additional Windows regressions cover interrupted publication, conflicting targets,
slow destination probes, Unicode settings reloads and blocked metadata enrichment.
The independent validators use Xiph libraries or the
Xiph FLAC CLI only in the development environment; the DLL does not depend on them.
Vendored MinHook, libogg and CEF declarations retain their upstream licenses.

## Artwork and additional metadata

The metadata worker prefers the largest available validated original Spotify
album cover over the Windows media thumbnail, which may contain branding. Windows artwork
remains the fallback if the original is unavailable. Image dimensions and depth
are written into FLAC PICTURE metadata. Artwork downloads and metadata lookups
run outside menu and playback-tracking callbacks. A separate bounded enrichment
worker keeps slow artwork/cache requests from delaying the client polling heartbeat.
Fresh basic snapshots retain already discovered fields for the same recording.

Available extra tags include artists, album artists, release date/year, track and
disc numbers/totals, label, ISRC, language, copyright and lyrics. Missing fields
are omitted. Requests use Spotify's existing client service and remain bounded;
unsupported service shapes retain basic tags. Lyrics are not guaranteed for every
track. Synchronized lyrics are embedded as millisecond LRC text in `LYRICS`,
with plain text used when timing is unavailable or invalid. No sidecars are created.
After a complete replay, verified existing FLAC and Ogg files can receive missing
text metadata and missing artwork even when no audio upgrade is planned. Existing
populated values, compressed audio and artwork are preserved. Plain lyrics can
upgrade to synchronized lyrics; existing synchronized lyrics are retained. A
lower-quality replay can enrich an existing FLAC without replacing its audio.
`Lyrics=0` disables lyric enrichment while allowing other metadata fills;
`Metadata=0` disables metadata-only updates. Updates are logged separately and do
not add a Latest Downloads entry. There is no bulk library scan.
