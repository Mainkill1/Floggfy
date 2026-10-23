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
numbers omit the numeric prefix. Cover artwork and title, artist, album, album
artist and track number are embedded in the single audio file. No image/JSON
sidecars or per-song directories are created. Buffers remain in RAM until a
complete listen and complete stream have been verified. Directories are created
only when publishing a file.

An existing target is skipped at equal or higher quality. A Vorbis upgrade requires
higher known nominal bitrate and no sample-rate reduction. FLAC upgrades require
matching channel count, no bit-depth/sample-rate reduction, and a verified
increase. Existing FLAC prevents saving a lower-quality Vorbis counterpart.
Unknown quality is preserved. A quality upgrade stages a **fully compiled** file
beside the old one and replaces it atomically; no track fragments are staged.

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
total sample count are currently rejected. Same-title repeat/loop handling needs
additional live coverage. Metadata fields absent from the public Windows session
are not invented. Audio targets are discovered from invariant decoder instructions
and Windows x64 function metadata; missing or ambiguous matches disable capture.
Menu integration checks its supported CEF version and structure sizes.

Activity and opt-in diagnostics are in `Floggfy.log` in the selected save root.
At 5 MiB, the log removes the oldest complete lines to make room for each new
entry. Logging and INI persistence use separate workers.
Completed saves log the captured audio format/quality and final tagged file size
in MiB and bytes. Upgrade, lyric-only update, skipped-file, changed-setting and
failure outcomes are distinct. Equal/better existing audio is identified only
when the on-disk format is verified; incomparable or unverified files get an
explicit retention reason.

The last ten successful saved paths are held in memory for this Spotify run.
**View last DLs**, directly below **Save Location**, is hidden until the first
successful download. Selecting a song opens Explorer with that file selected.
Skips, failed writes and lyric-only updates do not add history entries. Menu
snapshots stay fixed while open so a new download cannot redirect an existing
item's click target. Menu construction performs no filesystem or shell work.

Tagging, quality checks and publication run in a bounded background worker;
pending compressed save buffers count against the configured allocation budget.

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
duplicate skips, failed upgrades, atomic replacements and preference persistence.
The independent validators use Xiph libraries or the
Xiph FLAC CLI only in the development environment; the DLL does not depend on them.
Vendored MinHook, libogg and CEF declarations retain their upstream licenses.

## Artwork and additional metadata

The metadata worker prefers the largest available validated original Spotify
album cover over the Windows media thumbnail, which may contain branding. Windows artwork
remains the fallback if the original is unavailable. Image dimensions and depth
are written into FLAC PICTURE metadata. Artwork downloads and metadata lookups
run outside menu and playback-tracking callbacks.

Available extra tags include artists, album artists, release date/year, track and
disc numbers/totals, label, ISRC, language, copyright and lyrics. Missing fields
are omitted. Requests use Spotify's existing client service and remain bounded;
unsupported service shapes retain basic tags. Lyrics are not guaranteed for every
track. Synchronized lyrics are embedded as millisecond LRC text in `LYRICS`,
with plain text used when timing is unavailable or invalid. No sidecars are created.
After a complete replay, existing FLAC files can receive missing lyrics or upgrade
plain lyrics to synchronized lyrics while retaining their compressed audio,
artwork and other metadata. This also works when the replay uses lower-quality
audio. Already synchronized lyrics are retained. Audio-quality upgrades preserve
available existing lyrics when lyric metadata is enabled. Other equal- or
higher-quality audio is skipped; there is no bulk library scan.
