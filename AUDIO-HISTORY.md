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
UpdateExistingMetadata=1
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
- **AAC:** INI-only, default `0`. Set `AAC=1` and restart to capture supported
  low-quality SpAC/AAC streams as M4A. Downloads still controls all saving.
- **Metadata:** enriches tracks using existing client state, cached metadata, and Spotify's authenticated track/album services. Set 0 and restart to disable its hooks.
- **UpdateExistingMetadata:** default on. Set 0 to disable metadata-only changes
  to existing files, including lyrics and artwork upgrades. New audio saves and
  quality replacements still follow Downloads and the format settings.
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
The resolver checks existing deterministic names before allocating a vacant one,
so deleting the ordinary filename does not hide a previously suffixed recording.
It also checks the text-identity suffix used before URI enrichment. Searches are
bounded to 65 names without scanning directories; inaccessible possible matches
defer a new save instead of risking a duplicate.

A Vorbis upgrade requires higher known nominal bitrate and no sample-rate
reduction. FLAC upgrades require matching channel count, no bit-depth/sample-rate
reduction, and a verified increase. All saves first write and flush a completed
file beside the destination, then promote it without overwriting an unexpected
target. Upgrades reserve the verified old file before promotion; an interruption
can briefly leave the usual path absent, but keeps the old recording recoverable.
The next publication attempt recovers the retained recording without overwriting
a conflicting target. No track fragments are staged during playback.
Before an audio upgrade is staged, the old file is locked against modification
and its metadata is merged into the replacement. Populated and repeated user
fields, synchronized lyrics and existing artwork survive. Missing fields and
better validated covers follow the same policy as metadata-only updates;
`HISTORY_*` fields describe the new audio. Opaque artwork is retained rather than
replaced speculatively. The new compressed audio is not re-encoded.

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
the original two-second title window remains available after provisional repeat
confirmation. A continuous delayed title relabels that generation without
duplicating the previous completion. Seeks and excessive observation gaps remain
conservative rejection cases. Metadata fields absent from the public Windows session
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
A natural playback transition retains a matching unfinished capture for the
completed listen's existing ten-second completion window, allowing a late final
codec callback. It must still pass completeness, duration and unique-association
checks before publication. Retention ends when the listen is handled or expires.
Existing-file inspection uses checked reads with a separate temporary buffer
capped at 512 MiB; storage errors reject the inspection without stalling capture.
Basic Windows track/timeline reads run on a dedicated media observer, including
their WinRT waits. Capture consumes one sequenced RAM snapshot at a time without
waiting for that observer. Changed session/timeline data is retried rather than
paired with stale properties. Snapshots keep their observation timestamp; they
are not replayed as fresh listens. More than three seconds without a usable
observation still invalidates listening coverage, while decoder events continue
to drain. Stopping an observer does not wait for a stalled COM operation, and
another observer cannot start until the stopped one has finished. This worker
does not fetch artwork.
Destination resolution returns a verified matching-file snapshot or an explicit
vacant/blocked outcome. Publication reuses that snapshot for quality and missing
metadata decisions; replacement still revalidates the locked object. The bounded
collision search still examines occupied deterministic names to retain the best
matching recording, rather than scanning directories or maintaining another cache.

## Troubleshooting

1. Quit Spotify and set **Metadata=0**, then restart. This isolates client metadata
   service/cache discovery changes while retaining basic Windows media tags and
   timeline observations. Audio still saves without a cover.
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
Vendored MinHook, libogg, stb_vorbis and CEF declarations retain their upstream licenses.
Existing Ogg inspection checks CRCs, packet/sequence/EOS coverage and supported
Vorbis identification/setup headers. A bounded stb_vorbis adapter parses those
headers; it does not independently decode the audio packets. Setup inspection
uses an 8 MiB arena, a 1 MiB setup-packet limit, at most eight channels and bounded
codebooks. Unsupported headers are preserved as unverified files, never treated
as incomplete owned audio that permits replacement. Full decoder validation
remains a separate development check.

## Artwork and additional metadata

Spotify client/services provide the only active external album-artwork source.
Validated covers are embedded with their image dimensions and depth. Artwork is
optional: complete audio and available text tags are saved if no cover is ready.
No Windows media-session thumbnails are fetched, and there is no thumbnail worker,
queue or identity cache. Basic Windows track metadata and timeline observations
remain available for capture, listen tracking and menu information.

A separate bounded Spotify enrichment worker keeps slow artwork/cache requests
from delaying the client polling heartbeat. Fresh basic snapshots retain already
discovered fields for the same recording. `Metadata=0` disables this enrichment;
new files may lack artwork unless suitable artwork is already embedded in the
captured source. Recording-integrity checks and existing-cover preservation during
eligible audio upgrades remain active regardless of this setting.

Available extra tags include artists, album artists, release date/year, track and
disc numbers/totals, label, ISRC, language, copyright and lyrics. Missing fields
are omitted. Requests use Spotify's existing client service and remain bounded;
unsupported service shapes retain basic tags. Lyrics are not guaranteed for every
track. Synchronized lyrics are embedded as millisecond LRC text in `LYRICS`,
with plain text used when timing is unavailable or invalid. No sidecars are created.
After a complete replay, verified existing FLAC and Ogg files can receive missing
text metadata and missing artwork even when no audio upgrade is planned. Existing
populated text and compressed audio are preserved. Better validated Spotify
client covers may replace smaller or unmarked legacy thumbnails without reducing
resolution. Identical images, higher-resolution covers and artwork whose format
or layout cannot be safely assessed are retained. Plain lyrics can
upgrade to synchronized lyrics; existing synchronized lyrics are retained. A
lower-quality replay can enrich an existing FLAC without replacing its audio.
`Lyrics=0` disables lyric enrichment while allowing other metadata fills;
`Metadata=0` or `UpdateExistingMetadata=0` disables metadata-only updates. The
update planner reuses the quality inspection and skips reopening, retagging or
staging an existing file when no metadata or artwork changes are needed. Updates are logged separately and do
not add a Latest Downloads entry. There is no bulk library scan.

## Optional low-quality AAC streams

Spotify may supply SpAC AAC rather than Vorbis at low quality. `AAC=0` leaves this
capture path disabled by default, including its optional native hooks. `AAC=1`
uses the existing bounded capture/publication workers; it adds no worker or menu
control. Dynamic capability discovery verifies the reader constructor, input
method and vtable; an unavailable AAC capability leaves FLAC/Ogg support active.

SpAC pages use Ogg-like lacing, a different checksum and AAC-specific setup data.
Each page checksum and frame index must agree, capture must start at frame zero,
and the complete packet count must match rendered samples plus source priming.
Seeks, gaps, unsupported headers, lost events and memory limits prevent saving.
The parser retains at most one 64 KiB page; its allocation is included in capture
accounting. AAC contexts are limited to sixteen. Raw captures remain in RAM until
a complete listen and stream are associated. Publication remuxes unchanged access
units and exact decoder configuration into M4A with sample-exact edit timing.
The container inspector checks headers, sample tables, offsets and timing; it does
not independently decode AAC payloads. Supported profiles are AAC-LC, HE-AAC and
HE-AAC v2 with up to two rendered channels; xHE-AAC/USAC is currently unsupported.

M4A files embed available tags, synchronized lyrics and validated Spotify artwork.
Art remains optional. Metadata-only changes preserve compressed media and unknown
metadata, honor `UpdateExistingMetadata=0`, and do not add Latest Downloads rows.
A verified matching FLAC or Ogg suppresses a new low-quality AAC counterpart.
An unreadable preferred-format candidate defers publication until a later complete
listen; it does not count as an absent file or permit an extra M4A beside it.
Existing AAC renditions are retained: variable bitrate/file size does not prove a
quality upgrade. Collision naming, identity verification and atomic publication
use the same safeguards as other recordings. The Song shortcut also finds M4A.

The bounded minimp4 muxer retains its upstream license in
`native/vendor/minimp4`; no external player, decoder or network service is required
for production AAC capture.
