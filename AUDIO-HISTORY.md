# To Disk audio history

The native `version.dll` saves complete audio streams and provides an optional legacy
connectivity repair for the Windows x64 Spotify client. It copies the compressed
Vorbis, FLAC, MP3, or optionally AAC bytes that Spotify already reads for playback. It
does not request tracks, run FFmpeg, transcode audio, or require an external
runtime helper.

## Requirements and startup

- Use the Windows x64 Spotify desktop app with the x64 Floggfy files.
- Intel and AMD x64 processors are supported; PE's `AMD64` name refers to the
  architecture, not an AMD-only build. x86 and native ARM64 Spotify are unsupported.
- The GCC/C++ runtimes are statically linked. No separate Visual C++ Redistributable
  is required by Floggfy; its remaining imports are Windows system libraries.
- Launcher mode checks both adjacent binary headers before stopping existing
  Spotify processes and reports architecture/header-read errors. This does not
  validate every loader dependency or guarantee Spotify-version compatibility.
- Launcher mode also loads the adjacent `Floggfy.dll` into ordinary
  `Spotify.exe` network helpers before application entry. Chromium-managed
  suspended sandbox processes are forwarded unchanged.
- A mismatched automatic-mode DLL cannot run its own error-reporting code.

## Settings

`SpotifyHistory.ini` is included with the release and is created beside
`version.dll` on first launch when missing.
The top-left **To Disk** menu keeps Downloads, Latest Downloads, song details
and optional Capture Speed/Never Pause controls together. **Allowed Formats**
contains Ogg, FLAC, AAC, Podcasts and Include Lyrics (last). **Misc** contains
Save Location, Auto Skip Downloaded, Block Updates and the optional Network Kill Switch. **Fallbacks** contains
Windows Playback Fallback.
Checkbox labels and capture switches change immediately when clicked. INI
persistence runs in a background worker.

**Never Pause** sends Resume/Play when local Spotify is paused, including manual
pauses. `NeverPause=-1` (default) hides and disables the control; `0` shows it off;
`1` shows it on. It uses the existing playback poll, retries no more than once
every two seconds, and waits for an outstanding command to settle before retrying.
Changing the menu toggle preserves current capture buffers. Restart after INI edits.

```ini
;
; Floggfy Configuration File
; Options configured to be hidden or INI only can be unsafe to use
;
[To Disk]
Downloads=1
; Enable or Disable the To Disk menu drop down.
Menu=1
; Blank uses Windows %USERPROFILE%\Music\Spotify
; Custom paths must be absolute. Example: C:\Spotify
Save Location=
; Allowed Download Formats (original streams, no conversion)
; AAC is usually low quality; disabled by default.
Ogg=1
Flac=1
Aac=0
; Podcast support is not heavily validated. Should be fine?
Podcasts=1
; Metadata (lyrics and updates require Metadata=1)
Metadata=1
Lyrics=1
; Never Pause: -1=hidden, 0=off, 1=on.
NeverPause=-1
; Skip songs that are found to be already downloaded.
AutoSkipDownloaded=0
; Speeds: 1, 2, 4, 8, 10. Acceleration can cause choppy playback and throttling.
; 0 is same as 1, -1 hides it from the menu.
; Accelerated use may trigger account restrictions.
CaptureSpeed=-1

[Fallbacks]
; Look for missing track metadata in Spotify's disk cache.
ReadSpotifyDiskCache=1
; Request missing track and album tags from Spotify.
FetchMissingMetadata=1
; Allow artwork requests that can use the network.
FetchMissingArtwork=1
; Request missing lyrics when Include Lyrics is enabled.
FetchMissingLyrics=1
; Use Windows media state if Spotify's app playback tracking is unavailable.
WindowsPlaybackFallback=0

[Debug]
; Logging: 0=off, 1=finished/warnings/failures, 2=activity, 4=diagnostics.
; Logs use Save Location; each is capped at 5 MiB.
LogLevel=1
; These diagnostic switches require LogLevel=4.
DebugConnect=0
; Extra network observations; adds an optional hook.
DebugNetworking=0
; Callback and worker timing; adds diagnostic overhead.
DebugHookDiagnostics=0
; Saves raw streams and matching metadata to Save Location\DumpStreams; restart required.
DumpStreams=0

[Matcher]
; New media starts before retiring an unmatched stream (1-100).
MaxMatchQueue=5
; Time offset tolerance in milliseconds (0-120000).
TimeOffsetToleranceMs=10000
; Duration tolerance in milliseconds (0-2000).
DurationToleranceMs=25
; Save complete unidentified streams in Unmatched after MaxMatchQueue new starts.
SaveUnmatched=1

[History]
; RAM limit in MiB (8-1024).
MaxBufferedMiB=512
; Max MiB scanned per song looking for missing metadata
MetadataCacheReadMiB=256

[Telemetry]
; Logs blocked request details and payloads to Save Location\Floggfy-telemetry.log
BlockTelemetry=0
BlockEventReports=1
BlockPlaybackReports=1
BlockGrowthAnalytics=1
BlockSentry=1
BlockCrashReports=1
BlockInternalLogging=1
BlockTrackErrors=1
BlockAITelemetry=1
; Requires BlockTelemetry=1. Takes effect after restarting Spotify.
; Deletes known pending-tracking.pb queues at startup and normal exit; blocks their known tracking destinations.
BlockQueuedTelemetry=1
DumpTelemetry=0

[Misc]
; Block updates by denying access to the local Spotify Update folder.
BlockUpdates=0
; Work around Spotify falsely reporting no internet. AKA NCIS patch.
NetworkPatch=0
; Nuke all networking abilities (AKA Offline mode only with no telemetry)
; -1 hidden from menu, 0 disabled, 1 enabled
NetworkKillSwitch=0
; Force all supported telemetry blocking while offline; preserve Telemetry settings.
; Menu toggles restart Spotify; reconnect clears known queues after all helpers stop.
NetworkKillSwitchTelemetryLock=1
; Load Floggfy into ordinary Spotify children so Network Kill Switch covers network helpers.
; Caller-suspended Chromium sandbox processes are left unchanged.
; 0 disabled, 1 launcher/Floggfy.dll only, 2 all load methods including version.dll.
HookAllSubProcesses=1
; Fill missing tags, artwork and lyrics in existing recordings.
UpdateExistingMetadata=1
; Adds the track number infront of songs
TrackNumberInFilename=0
; Remove old number prefixes; requires TrackNumberInFilename=0.
ConvertNumberedFilenames=0
```

- **Downloads:** master switch. Start the next track from its beginning after
  enabling capture; enabling halfway through a track cannot recover earlier audio.
- **Menu:** set `0` and restart Spotify to disable menu integration. Configured
  capture and any explicitly enabled network patch operate independently.
- **NetworkPatch:** `[Misc]`, INI-only, default `0`; missing keys also disable the patch.
  Set `1` and fully restart Spotify to enable the legacy NLM compatibility hooks.
  Disabled startup skips import discovery, IAT/vtable changes and hook retries;
  audio capture, metadata and menu startup remain independent. The option is
  loaded once at startup; no live detachment of COM hooks is attempted. Missing
  keys are added through the existing atomic settings writer, preserving other
  INI content. Ordinary menu saves preserve existing restart-only entries
  (`NetworkPatch`, `DumpStreams`, `SaveUnmatched`, matcher settings and debug switches),
  including edits made after startup; only missing keys receive defaults.
- **BlockUpdates:** `[Misc]`, default `0`. The Misc checkbox denies the current
  Windows user read/write access to `%LOCALAPPDATA%\Spotify\Update`. Enabled
  startup verifies and repairs the restriction. Disabling restores permissions
  from the parent Spotify folder; no old ACL snapshot is kept. The restriction
  remains on disk after Spotify exits. Disable it before removing Floggfy if
  updates should resume. This targets the desktop update folder, not updates
  through other paths or accounts.
- **NetworkKillSwitch:** `[Misc]`, default `0` shows the control off. `-1` hides it and
  disables its hooks; `1` shows it on. With `NetworkKillSwitchTelemetryLock=1`
  (default), menu changes restart Spotify. Enabling immediately blocks networking
  and starts a session with every supported telemetry category enabled. Disabling
  stops Spotify and its helpers, deletes known `pending-tracking.pb` queues, and
  restarts with the saved telemetry preferences. Cleanup failure keeps the switch
  enabled. Telemetry INI values are never changed by this override. Set the lock
  option to `0` for the previous live network-only toggle. It blocks
  covered IPv4/IPv6 Winsock operations in processes loading Floggfy, including
  functional traffic needed for login, playback and online metadata.
  `HookAllSubProcesses=1` (default) loads `Floggfy.dll` into ordinary
  `Spotify.exe` children, including the network helper, before entry. `0`
  disables child loading. `2` applies the same behavior to every Floggfy load
  method, including automatic `version.dll`. Chromium-managed launches that
  already request `CREATE_SUSPENDED` are forwarded unchanged so their sandbox
  setup retains control of startup.
  Already queued I/O is not cancelled; uninjected
  processes, external DNS services,
  provider pointers obtained before hook startup, direct native I/O and cached
  Registered I/O (RIO) paths remain outside coverage. This is not a firewall
  guarantee. If hooks are unavailable, the menu shows **Unavailable**.
- **ReadSpotifyDiskCache** searches Spotify's stored track metadata within the
  MetadataCacheReadMiB limit. Defaults to on.
- **FetchMissingMetadata** enables Spotify requests for missing track and album tags.
  Defaults to on.
- **FetchMissingArtwork** enables artwork requests that can use the network.
  Available artwork can still be reused, including direct reads from Spotify's image cache.
  Defaults to on.
- **FetchMissingLyrics** enables Spotify lyrics requests when **Include Lyrics**
  is enabled. Turning it off still lets **Include Lyrics** save available lyrics.
  Defaults to on.
- **WindowsPlaybackFallback:** `[Fallbacks]`, default `0`, also available in the menu. Enable the Windows
  media-session observer only when Spotify's app playback tracking is unavailable.
- **Save Location:** empty defaults to the Windows Music known folder's `Spotify`
  subfolder, including redirected Music folders. A custom directory is accepted.
- **FLAC/Ogg:** select which original source formats to retain. Enabling FLAC does
  not convert Vorbis playback into lossless audio. Spotify must supply FLAC.
- **Capture Speed:** default `-1` hides the menu and uses 1x. `0` or `1` shows
  the menu at 1x; `2`, `4`, `8` and `10` select faster recording. Accelerated capture
  shortens the decoded PCM output span only after Spotify has consumed the full
  compressed input span, so saved bytes are not resampled or transcoded. It is
  intended for muted, unattended capture. Live sound can be choppy, Spotify's
  progress display may not track real time, and changing speed invalidates the
  current track so it must be replayed from the beginning. If the decoded-output
  hook is unavailable or becomes invalid, Floggfy safely falls back to `1x`.
- **AAC:** default `0`; the **Allowed Formats** toggle enables capture of new
  SpAC/AAC streams as M4A when supported. Start playback from the beginning after
  enabling it. Downloads still controls all saving.
- **Podcasts:** menu/INI toggle, default `1`. When enabled, complete identified
  `spotify:episode:` items retain their original MP3, Ogg, FLAC, or enabled AAC
  stream under `Podcasts\<Show>`. No PCM recording or transcoding is used.
  Changing the toggle preserves in-flight audio; the setting applies when publishing.
- **MaxMatchQueue:** `[Matcher]`, default `5`, range 1–100. Retire unresolved
  streams after this many distinct subsequent media starts, including repeats.
  Pauses, buffering and metadata polling do not advance the count.
- **TimeOffsetToleranceMs:** `[Matcher]`, default `10000`, range 0–120000 ms.
  Bounds the relative start-time offset for conservative metadata matching;
  `0` requires exact playback IDs. Exact IDs can match outside this window when
  native ownership evidence belongs to the capture lifecycle.
- **DurationToleranceMs:** `[Matcher]`, default `25`, range 0–2000 ms. Allows
  small differences between measured audio length and metadata duration.
  Neither timing setting can make incomplete audio valid. Restart after edits;
  legacy `TimingToleranceSeconds` migrates to milliseconds when the new key is absent.
- **SaveUnmatched:** `[Matcher]`, default `1`. At unmatched retirement, save only
  complete, codec-validated supported audio under `Save Location\Unmatched`,
  with its audio extension and a JSON correlation sidecar. Incomplete or corrupt
  preloads are discarded. Set `0` to discard unmatched audio too. Failed complete
  writes retain their bytes for retry. Existing `[To Disk]` values migrate to
  `[Matcher]`. Raw `DumpStreams` output remains separate.
- **Metadata:** enriches tracks using existing client state, cached metadata, and Spotify's authenticated track/album services. Set 0 and restart to disable its hooks.
- **UpdateExistingMetadata:** default on. Set 0 to disable metadata-only changes
  to existing files, including lyrics and artwork upgrades. New audio saves and
  quality replacements still follow Downloads and the format settings.
- **Lyrics:** default on. Requests available lyrics automatically through Spotify's
  existing client service, even with the lyrics panel closed. Set 0 in the INI or
  use the menu toggle to stop lyric discovery, cancel pending lyric work, and
  disable lyric enrichment while retaining other metadata and artwork.
- **LogLevel:** `[Debug]`, default `1`; `0` disables logging, `1` records newly finished
  tracks plus warnings/failures, `2` also records visible lifecycle activity,
  and `4` enables the main diagnostic lane. `Floggfy.log` contains general
  activity and outcomes; troubleshooting detail goes to
  `Floggfy-diagnostics.log`. Level 4 also adds
  decoder-ownership evidence using bounded numeric capture, owner, and playback
  tokens. Raw native addresses, playback IDs, and URIs are never written in
  ownership records. For a structurally verified FLAC/Ogg decoder, only an exact
  native playback identity tied to that capture generation resolves overlapping
  metadata candidates. Otherwise the matcher requires a unique conservative
  association; missing, conflicting or lost evidence cannot establish identity.
  Matcher diagnostics include start/end/duration deltas in milliseconds; the
  end delta is diagnostic only. Changed `matcher-counts` records report stream
  and metadata pushes, pops and pending counts, plus matched, saved, skipped and
  unmatched outcomes, at most once per second. These counters observe retained
  records and publication; they never authorize a match or save.
- **DumpStreams:** `[Debug]`, default `0`; set `1` and restart for forensic capture,
  independently of LogLevel, download switches and format filters. It writes
  continuous consumed decoder input to `Save Location\DumpStreams\<UTC start
  timestamp>\`, with timestamped `.bin` files and matching `.jsonl` metadata
  logs. Decoder reuse and media changes keep appending to the same decoder/epoch
  file. `session.jsonl` links input groups, byte offsets, callback start/end times,
  native playback IDs and app metadata/state observations. Metadata without a
  matching native ID is marked `association: unresolved`. Missing ranges are
  explicit gap records; physical offsets describe the saved file, while logical
  offsets include known omitted input. Unknown loss makes logical offsets null.
  Dumps survive ordinary recording rejection and are retained until deleted.
  A clean stop adds `session_end`; its absence indicates an interrupted session.
- **DebugConnect:** `[Debug]`, default `0`; requires `LogLevel=4`. Enables cached
  Connect target observations and passive local action requests. `0` opts out of
  those observations even with deep networking enabled. No discovery, playback
  transfer or extra network request is performed. Basic playback identification
  remains independent of this diagnostic option and of `Metadata`.
- **DebugNetworking:** `[Debug]`, default `0`; requires `LogLevel=4`. Enables the
  read-only decoded Connect observer and allowlisted enrichment I/O observations
  in `Floggfy-networking.log`. No raw payloads, media, credentials, URLs or
  real device identifiers are written. Receipt is distinct from command execution;
  unobserved traffic and initiators remain unknown. This optional decoded-message
  hook is independent of `NetworkPatch` and the `DebugConnect` cached-state option.
- **DebugHookDiagnostics:** `[Debug]`, default `0`; requires `LogLevel=4` and is independent
  of `DebugNetworking`. It writes `Floggfy-hook-diagnostics.log` with callback
  and queue-lock profiling, interval timing
  drains, slow/pending menu and startup spans, and capture-worker progress checks.
  Basic debug mode performs no per-callback profiling. Completed spans at least
  10 ms, pending spans after 250 ms, and worker progress gaps after 1 s are sampled
  with bounded repeat reporting. These observations do not establish a crash or
  change capture acceptance. There are no dumps or additional suspension hooks.
  Launcher Windows Debug APIs remain required for startup/DLL-load synchronization.

Debug settings live under `[Debug]` and require a full Spotify restart after
INI edits. `NetworkPatch` remains a separate `[Misc]` startup option.

| Effective settings | `Floggfy.log` | Main diagnostics | Networking | Hook diagnostics |
| --- | --- | --- | --- | --- |
| `LogLevel=0` | Off | Off | Off | Off |
| `LogLevel=1` | Finishes, warnings, failures | Off | Off | Off |
| `LogLevel=2` | Adds starts, upgrades, renames and other lifecycle activity | Off | Off | Off |
| `LogLevel=4` | Same lifecycle events | Playback, metadata and ownership detail | Off | Off |
| Above + `DebugNetworking=1` | Same | Same | Sanitized networking observations | Off |
| Above + `DebugHookDiagnostics=1` | Same | Same | Independently controlled | Timing, slow/pending activity and progress |

`DebugConnect=0` always disables its cached Connect/action observations;
`DebugHookDiagnostics=0` always disables callback profiling. An unavailable save
root is an operational failure, but cannot guarantee disk delivery into that same
unavailable destination. Each current output has a 5 MiB cap. Normal and optional
records use separate bounded 128-entry queues, so diagnostic saturation cannot use
normal queue slots. One worker drains bounded batches fairly. Records retain their
enqueue-time timestamps; per-queue omission summaries report contention/overflow
without globally deduplicating real events.

- **MaxBufferedMiB:** `[History]`, 8–1024 MiB, default 512, limits compressed capture allocations.
  There is also an approximately 16 MiB event queue sized for bounded 10x
  transition bursts, artwork, and a temporary
  completed-file buffer during native tagging. Oversized tracks are discarded.

- **MetadataCacheReadMiB:** `[History]`, default `256`, range 1–4096 MiB. Limits
  disk reads for missing cached metadata per lookup; background reads have no fixed
  time cutoff.
- **Telemetry:** `[Telemetry]` keeps `BlockTelemetry=0` as its master switch, with
  separate category toggles and `DumpTelemetry=0` for readable blocked-request
  logs. See [privacy settings and testing](docs/telemetry-testing.md).

`DebugConnect`, `DebugNetworking`, `DebugHookDiagnostics` and `NetworkPatch`
accept only an unquoted `1` (surrounding
spaces/tabs are allowed). `0`, blank, malformed and duplicate entries disable
the corresponding observation/patch; invalid existing entries are not rewritten
by menu saves. All four default to `0`. Changes still require a full restart. Preservation applies
to the INI text read for that save; simultaneous writes from an external editor
are not a cross-process transaction.

## Files and duplicates

Default music paths are:

```text
<Windows Music>\Spotify\Artists\Artist\Album\Title.ogg
<Windows Music>\Spotify\Artists\Artist\Album\Title.flac
<Windows Music>\Spotify\Podcasts\Show\Episode.{mp3,ogg,flac,m4a}
```

Album folders have no year prefix. New filenames use the title alone by default.
`TrackNumberInFilename=1` adds `NN - ` when a track number is known; disc numbers
remain in metadata. Existing title-only, numbered or disc-prefixed filenames
are recognized and reused; switching naming mode never authorizes a second copy.
Title, artist, album, album artist and track/disc numbers are embedded
in the single audio file.
If a filename is occupied and no matching recording can be verified, new audio
is saved under `Duplicates` with the same folder hierarchy. Valid Spotify track
IDs use RC6's `[t-<id>]` suffix. Missing or malformed track IDs use
`[no-id-<16-digit hash>]` so unresolved identities are visible during manual review.
The hash is a naming aid; recording metadata still controls reuse and upgrades.
Unreadable candidates defer saving. Existing hex-suffixed files remain discoverable
in their original folders; this does not migrate an existing library.
Validated cover artwork is included when available; missing artwork does not
discard completed audio. Identified saves create no image/JSON sidecars or
per-song directories. Stream buffers and metadata history are collected
independently; one matcher/finalizer joins them and schedules publication after
codec completeness and identity are verified. A completed listen or final UI tick
is not required. Successful publication or a verified existing-file match releases
the audio buffer; retries retain it. Directories are created only when publishing.

The INI-only naming options default to `0` and take effect after restarting:

```ini
TrackNumberInFilename=0
ConvertNumberedFilenames=0
```

With conversion enabled, playback starting queues a check of existing recordings
on the publication worker. It finds the best verified saved recording and removes
only an exact Floggfy-generated track/disc prefix from a complete owned file. The
same-directory atomic rename leaves every audio/metadata byte intact, preserves
identity suffixes and never overwrites an occupied filename. Conversion runs while
Downloads is enabled; it does not require a completed listen or metadata updating.
If both options are `1`, numbering wins and conversion is reset to `0` in the INI.
The two settings never compete to rename a file back and forth.

The check lists each known album directory once, then compares expected
FLAC/Ogg/M4A/MP3 names and legacy aliases against those lists in memory. Only listed
matching candidates are opened for identity/quality validation; absent generated
names do not cause individual filesystem probes. The list is discarded after
the operation.

The check uses one coalesced pending request on the existing publication worker;
rapid track changes discard superseded requests and audio saves take priority.
There is no library-wide sweep or capture/menu-thread filesystem I/O. Audio
publication also provides a conversion fallback if its start check could not run.
Renames do not enter Latest Downloads; existing session entries keep their order
and follow the new path. Locked, ambiguous or unowned files remain intact. An
occupied destination does not trigger duplicate deletion or an overwrite.

Existing audio is skipped at equal or higher quality only after validating its
complete container and recording identity. Spotify track URI takes precedence;
without it, the full textual identity, disc/track numbers and duration must agree.
Sanitized or shortened names that collide use a deterministic suffix. Unrelated
or unverified files are preserved. A verified complete FLAC suppresses a Vorbis
counterpart only for the same recording.
The resolver checks existing deterministic names before allocating a vacant one,
so deleting the ordinary filename does not hide a previously suffixed recording.
It also checks the text-identity suffix used before URI enrichment. Searches are
bounded to 195 known names per folder including old numbered/disc prefixes.
The known album artist names the artist folder and the `ALBUMARTIST` tag;
contributing/guest performers belong in `ARTIST`. Longer contributor lists never
expand a populated album artist. The current client's explicit album-artist value
wins over older cached lists. A known Windows album artist remains authoritative
when client cache enrichment disagrees. One exact legacy folder named from
expanded album/contributor credits remains a lookup-only alias for files already saved there. No other artist folders are
searched, and new files use the album-artist folder. Existing populated tags are
preserved by the established metadata-only/audio-upgrade policy; this does not
bulk rewrite a saved library. At most two nonrecursive listings are shared across format lookups, each limited to 16,384 entries. Inaccessible or
partial listings and inaccessible possible matches defer a new save instead of
risking a duplicate. Final publication and renames still lock/revalidate the live file; a listing never authorizes a write.

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
Audiobooks/Author/Series/Book, Podcasts/Show, Music Videos and Concerts. These
folders are never precreated. Live Windows media metadata currently identifies
music, Various Artists and Spotify episodes; reliable category and book-series
metadata enrichment remains additional work. Video extraction is not implemented.

## Verification and limits

FLAC uses the original decoder's compressed-input callback and validates contiguous
decoded-frame sample coverage. Native metadata rewriting preserves STREAMINFO and
all compressed frame bytes, adds VORBIS_COMMENT and PICTURE blocks, and needs no
encoder. See `FLAC-SUPPORT.md` for the hook boundaries and FLAC verification.
Nine actual complete FLAC listens also passed full independent decoding and
original audio MD5 verification, with embedded artwork and metadata.
Personal recording details are not published.

Codec readers require source headers and contiguous valid audio through the
format's completion boundary. Missing pages/frames, decoder errors and truncated
sources prevent publication; a frame-aligned MP3 prefix alone is not completion
proof. FLAC requires its advertised total sample count; MP3 validates declared
Xing/VBRI counts when available and otherwise needs a real source boundary.
Independent metadata history retains stable playback generations, including
repeats and paused snapshots. Late, reordered or missing UI end ticks do not
invalidate complete captured bytes. Ambiguous identity waits for resolution or
unmatched retirement; queue saturation retries retain bytes, while explicit
memory limits can discard them. Skipping or seeking cannot manufacture missing
audio, but does not by itself reject an already complete stream.
Metadata fields absent from the public Windows session
are not invented. Audio targets are discovered from invariant decoder instructions
and Windows x64 function metadata; missing or ambiguous matches disable capture.
All hooks attempt discovery on unknown Spotify/CEF versions. Menu and metadata
integration check required exports and structure layouts without a version list.

Renderer console diagnostics use a dedicated bounded entry point. Reserved
`FLOGGFY_STATUS:` / `FLOGGFY_NETWORK_V1:` messages are consumed even when rejected,
and validated before log routing or native failure-keyword classification.
Known client status fields use the same allowlist with deep networking on or off;
retired readiness messages remain reserved and are consumed without output.
Unknown/malformed/oversized, embedded-NUL and non-ASCII renderer statuses are
dropped. Direct generic logger calls with these reserved prefixes also use the
same sanitizer, so failure words cannot copy a rejected payload into activity
logs. Explicit enrichment request summaries still require deep mode. Native
capture failures continue to log normally at `LogLevel=1` or higher.

Activity and outcomes have one producer-selected category and one destination.
Message wording, track titles and paths never determine severity. Successful
saves containing words such as “failed” or “unsupported” remain successful events.
Normal save/upgrade/skip/metadata/start outcomes are not mirrored as diagnostic
`SAVED`, `UPGRADED`, `SKIP`, `METADATA UPDATED` or `TRACK` confirmations.
Supplemental diagnostic records contain additional facts such as source, timeline
movement, raw status, capture epoch and queue lag. Optional private client fields
are reduced to change flags or rejected by the renderer allowlists.

There is no routine ten-second status heartbeat and no diagnostic format-sniff
capture detour. The FormatSniff discovery anchor and structural validation remain.
FLAC errors emit one normal failure and, when enabled, one useful context record.
`LogLevel=4` alone does not measure native callbacks, queue-lock times or empty FLAC
reads. With profiling explicitly enabled, counters and maxima are drained by a
separate periodic timing interval, including during pauses; empty intervals stay
silent. The logger performs file work; audio callbacks never do.

Connect diagnostics read the already-created `ConnectDevicesAPI` snapshot and
report `target=local`, `remote` or `unknown`, plus connection state and a device
change flag. Device names and IDs are never printed. `not_connected` is the
client's remote-playback link state, not evidence that the PC lacks Internet.
Local PlayerAPI action requests are recorded separately as `source=local-client`;
requests may be cancelled or fail, so they are not proof of a completed action.
A passive listener queues at most 16 allowlisted observations per poll, reports
omitted observations and leaves dispatch/prevention untouched. Opt-out removes
the listener; an API cleanup failure leaves at most one inert binding.

The current PlayerAPI snapshot does not expose an incoming command's sender.
A phone controlling playback on this PC is distinct from playback on the phone.
Neither a remote target nor absence of a local action proves who sent Pause:
controller/initiator stays `unknown`. Repeated unchanged paused snapshots stay
silent. Missing or changed optional APIs leave normal metadata collection intact.

The optional native decoded-message observer checks the command parser's string
anchors, unwind ownership, const-string ABI and complete-message call sites.
Plain JSON and finalized decompressed JSON are observed; the compressed envelope
parse is excluded. It uses one shared JSON-parser detour with a small pass-through
path for every unrelated caller. There is no TLS/key interception or packet dump.
The callback copies at most 16 KiB using OS-checked reads and a nonblocking
16-entry queue, calls the original parser once with the original arguments,
preserves its AL return and Windows last-error value, and leaves native objects
and command handling untouched. The existing startup worker drains observations;
no background thread is added. This optional shared detour's live timing and
interleavings remain unverified; it is off by default.

Only allowlisted actions, message IDs, receipt times and anonymous process-local
sender labels are logged. Other fields and raw bodies are not written to disk.
Queued payload bytes are cleared after consumption; sender correlation retains
at most 32 bounded identifiers privately for this process. Depth/size limits and
ambiguous selected JSON fields prevent summaries rather than affecting commands.
Unknown or unsummarizable decoded messages and omissions use bounded five-second
aggregate counters instead of one line per message. Real identical command
receipts remain distinct. Queue contention/overflow omits observations, never
commands. `json_ok=1` means
that the original JSON parser accepted syntax, not that dispatch succeeded.
`execution=unobserved` explicitly separates receipt from the playback result.
Sender fields are reported data, not authenticated human/device attribution.
No message modification or feature-probe rewriting mode is implemented.

Network context is only the last result of an already intercepted Windows NLM
query, with its age, HRESULT and original/effective connectivity flags. This
observation is normally unknown with `NetworkPatch=0`, since the NLM hook is not
installed. It does not perform another network request or establish that Spotify's audio service
is reachable. A pause observation does not identify its initiator; neither a
coverage rejection nor a client flag proves corrupted audio, a remote command,
a service outage or a process crash. These read-only diagnostics add no playback controls or exception handlers.
At 5 MiB, compaction retains approximately 4 MiB of newest complete lines before
adding the bounded incoming batch. This reclaims headroom rather than rewriting
nearly 5 MiB for each line. Whole-line boundaries preserve UTF-8, and slow or
unwritable destinations affect only the existing log worker. Logging and INI
persistence use separate workers.
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
FLAC, Ogg, M4A or MP3 file exists. When enabled, it opens the containing folder. A separate
worker checks destination paths; capture processing and menu rendering only
exchange bounded requests and memory snapshots. Superseded results cannot enable
a different track or save location, and `Menu=0` disables destination probes.
The shortcut first checks its 16-entry verified-file cache with a fresh file
signature. Unchanged saved files need no directory listing or audio reread.
Discovery lists the directory once and compares the four ordinary names before
legacy/collision aliases, stopping at the first complete identity match.

Quality, format and sample-rate rows appear only when their values are known.
During recording, songs and episodes use the codec headers of the capture bound
to current playback, including MP3, AAC, Ogg and FLAC. FLAC bit depth is shown
when known; lossy formats do not acquire an inferred PCM bit depth. A codec's
lossy/lossless classification does not imply a guessed Spotify quality tier.
Publication still chooses the best verified existing recording. An unavailable
Song target is retried after five seconds; changed tracks/settings and successful
publication refresh bypass that cooldown. Signatures that change trigger discovery
immediately. The cache holds no audio, artwork or directory listing. A pending row may acquire its verified target
when the submenu is prepared without changing the frozen song label or settings.
The click rechecks the file so deletion produces an unavailable notice instead of opening an
empty folder. Only audio saves create directories. Folder clicks do not add
download history entries.

Tagging, quality checks and publication run in a bounded background worker;
pending compressed save buffers count against the configured allocation budget.
Codec readers determine completion independently of playback observations.
Stream generations retain their own compressed bytes and timing; a separate
metadata history retains playback identities and labels. The matcher/finalizer
alone joins these records and queues publication. Complete identified audio can
save while playback continues, without a completed listen or UI end tick.
Unresolved streams remain bounded by `MaxMatchQueue` and the memory limit.
Existing-file inspection uses checked reads with a separate temporary buffer
capped at 1 GiB; storage errors reject the inspection without stalling capture.
Spotify's cached PlayerAPI is the primary playback source. Track identity uses the
Spotify URI and playback instance rather than a Windows title/artist snapshot. Progress comes from the
app's position-at-timestamp, timestamp and pause state, following the model used
by [Spicetify's Player wrapper](https://github.com/spicetify/cli/blob/main/src/jsHelper/spicetifyWrapper/core.js).
The native heartbeat requests `PlayerAPI.getState()` roughly every 500 ms through
the renderer. This reads the app's cached state without a Web API request; it
continues working when the optional update-event listener is unavailable.
PlayerAPI update events preserve short transitions between the native heartbeat
polls. A bounded queue retains those observations in order; lost observations
cannot establish a metadata association. A buffering transition carries the
app's event timestamp, but time spent buffering cannot manufacture missing audio.
Spicetify does not need to be installed. Basic playback observation remains active
with `Metadata=0`; that setting controls optional enrichment only.

The app source requires a confirmed local playback device. Remote playback and
buffering do not fall through to a possibly stale
Windows session. Renderer snapshots are bounded, strictly parsed and rejected
when older than three seconds. Clock differences up to 250 ms are tolerated
without moving an observation into the future. For the same URI and playback
instance, fresh Ready samples with nondecreasing renderer timestamps cannot
regress solely through UTC/uptime rounding within that 250 ms bound: their mapped
time is held at the preceding observation. Actual
source-clock regressions, identity changes, larger discontinuities and transition
event times retain the existing observation guards. These checks protect metadata
evidence; source bytes and codec validation establish audio completeness.

When app observations are unavailable or incompatible, the default behavior is
to keep playback verification unavailable. Set `WindowsPlaybackFallback=1` in
`[Fallbacks]` to opt into public Windows media sessions on a dedicated observer
thread. An empty first Spotify session no longer masks another usable session,
and a disconnected session manager is recreated. Windows fallback also requires
Spotify's **Settings > Display > Show desktop overlay when using media keys** to
be enabled.
Normal position/timestamp refreshes are accepted when movement agrees with
uninterrupted playback, allowing 250 ms of rounding difference and clamping at
the track end. Source/session changes, discontinuous jumps, regressing clocks
and observation gaps invalidate the affected playback observations. Capture consumes sequenced RAM
snapshots without waiting for COM, disk, enrichment or network work. Stopping an
observer does not wait for a stalled COM operation. No Windows artwork is fetched.

With `LogLevel=4`, `media-session` diagnostics report `source=spotify`
or `source=windows`, the failed stage, reason, HRESULT, session/candidate counts,
and numeric timeline values. Persistent failures are summarized at most every
30 seconds; recovery and source changes are reported immediately. These summaries
contain no track titles, URIs, device identifiers or raw renderer payloads.
The ordinary activity log warns when playback verification remains unavailable
for three seconds during active capture; brief app-start buffering stays in
diagnostics. This grace period affects reporting only and does not decide audio
completeness or ownership.
Saved files include `HISTORY_PLAYBACK_SOURCE` so validation can identify the source.
Native playback IDs can establish exact ownership only within the capture's
lifecycle. Conservative fallback requires a unique one-to-one match in both
directions, with positive decoder evidence and compatible time offset and duration.
Removing an alternative through retirement or eviction never proves ownership.
Decoder reuse and consecutive plays use distinct generations; an earlier native
identity observation cannot label a later recording merely because the address matches.

After `MaxMatchQueue` distinct subsequent starts, unmatched retirement saves only
complete codec-validated supported audio when `SaveUnmatched=1`; incomplete or
corrupt preloads are discarded. Pauses and ordinary updates do not advance that
count. Queue-full and disk-write retries retain the same bytes and association.
The save worker independently checks integrity before publication; successful
publication or a verified existing-file match releases the retained buffer.
Destination resolution returns a verified matching-file snapshot or an explicit
vacant/blocked outcome. Publication reuses that snapshot for quality and missing
metadata decisions; replacement still revalidates the locked object. The bounded
collision search still examines occupied deterministic names to retain the best
matching recording, rather than scanning directories or maintaining another cache.

## Troubleshooting

1. Quit Spotify and set **Metadata=0**, then restart. This isolates client metadata
   service/cache discovery changes while retaining basic Windows media tags and
   app playback observations. Audio still saves without a cover.
2. If playback is choppy or progress looks wrong, set **Capture Speed** to **1x**
   and replay the track from its beginning. If menu integration crashes or
   disappears, set **Menu=0** and restart. Configure Downloads, FLAC, Ogg and
   `CaptureSpeed` directly in the INI.
3. Set **LogLevel=4** under `[Debug]` temporarily. Inspect `Floggfy.log` and
   `Floggfy-diagnostics.log` in the configured save root. Check `media-session`
   lines for the selected source,
   failure stage, reason and HRESULT. `source=spotify` works independently of
   Windows media controls. For `source=windows` with `no-spotify-session`, enable
   Spotify's **Settings > Display > Show desktop overlay when using media keys**.
   Replay a fresh track from its beginning after observation recovers. Turn
   diagnostic logging back to level 1 afterward.
4. Confirm Downloads is on and the actual source format is enabled. Lossless
   playback produces FLAC; its Ogg parser is unused. Enabling FLAC cannot turn a
   lossy Spotify stream into lossless audio.
5. Start a fresh track at position zero and let it finish. Seeking, skipping,
   truncated input, decoder errors and ambiguous association prevent saving.
6. For unusually large lossless tracks, increase MaxBufferedMiB within the
   1024 MiB limit.
   Check the destination is writable and has space; try a local drive.
7. After a Spotify update, check the log for missing or ambiguous audio targets.
   The dynamic resolver supports shifted functions but safely rejects rewritten
   boundaries.
   Include your Spotify version and a short redacted failure excerpt in an issue.
8. To uninstall, quit Spotify and remove `version.dll`. Remove the INI if you no
   longer need its settings.


## Release build

The release build uses MinGW-w64 and Python 3.

```bash
bash build-native.sh
python3 package-release.py
```

The release DLL links production sources and libraries. Codec assertion code
and SQLite's internal test controls are excluded from the release objects.
The optional INI diagnostics are available in the release build.

## Development checks

Test fixtures and runners live under `tests/`. Test executables use their own
objects so they can enable assertions, failure injection and race checkpoints.
The portable suite also needs a native C/C++ compiler and Node.js.

```bash
bash tests/runners/test-native.sh
# Actual Windows file/settings/log tests, from WSL:
bash tests/runners/test-native-windows.sh
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
As soon as playback identifies them, verified existing FLAC and Ogg files can receive missing
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
capture path disabled by default. `AAC=1` uses the existing bounded
capture/publication workers; it adds no codec-specific worker. Dynamic capability
discovery verifies the reader constructor, input
method and vtable; an unavailable AAC capability leaves FLAC/Ogg support active.

SpAC pages use Ogg-like lacing, a different checksum and AAC-specific setup data.
Each page checksum and frame index must agree, capture must start at frame zero,
and the complete packet count must match rendered samples plus source priming.
Seeks, gaps, unsupported headers, lost events and memory limits prevent saving.
The parser retains at most one 64 KiB page; its allocation is included in capture
accounting. AAC contexts are limited to sixteen. Raw captures remain in RAM until
codec-complete audio is matched to metadata or reaches unmatched retirement.
Publication remuxes unchanged access
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

AAC extraction and M4A construction each preflight a conservative 512 MiB
operation budget. It includes retained input capacities and packet objects;
construction also reserves the vendor's full 128 MiB allowance, one media output
and bounded metadata/table scratch. The flattened SpAC source is released before
muxing. Muxing and tagging share one reserved output buffer and rewrite only its
terminal movie metadata; they do not duplicate all compressed samples for tagging.
Oversized operations fail before construction and leave existing recordings intact.
This budget is separate from capture buffering and is not a process-wide memory
cap. External metadata-only tagging retains its source and budgets its necessary
second output buffer. A resource-limit failure is distinguished from invalid or
unsupported AAC input. None of these checks transcode, downsample or truncate audio.

The bounded minimp4 muxer retains its upstream license in
`native/vendor/minimp4`; no external player, decoder or network service is required
for production AAC capture.

## Logging cleanup regression gate

Run `bash tests/runners/test-diagnostics.sh [SPOTIFY_DLL]` from WSL with the MinGW toolchain and
Windows executable interop available. It covers explicit categories, queue/file
isolation and saturation, renderer privacy and opt-outs, capped-log I/O counts,
read-only decoded command handling and aggregation, basic/profiling callback work,
format-detour removal, playback/quality guards, and exact embedded INI defaults.
The optional DLL is mapped read-only for current-build discovery; it is not
executed. Omit it to run synthetic discovery with an explicit skip of that check.
Native portable regressions remain `bash tests/runners/test-native.sh`; the full Windows and
startup/publication regressions remain `bash tests/runners/test-native-windows.sh`.

Illustrative before/after output (synthetic names, timestamps omitted):

Before, `Floggfy.log` with debug enabled:

```text
started Example Artist - Song
debug TRACK Example Artist - Song
debug SAVED ...Song.ogg
finished Ogg Vorbis ... | Example Artist - Song
debug FLAC decoder error; stream discarded
failed FLAC decoder error; stream discarded
```

After, `Floggfy.log`:

```text
started Example Artist - Song
finished Ogg Vorbis ... | Example Artist - Song
failed FLAC decoder error; stream discarded
```

At `LogLevel=4`, `Floggfy-diagnostics.log` adds facts rather than the save outcome:

```text
... source=spotify ... pos=0.018/231.000 ...
... FLAC observation ... epoch=2 ... queue_lag_ms=0.5 ...
... direction=local source=client-control request=pause source=local-client
... direction=local source=client-state paused=1 buf=0 ...
```

With `DebugNetworking=1`, `Floggfy-networking.log` separately records sanitized
network observations such as:

```text
... direction=in service=connect ... action=pause ... execution=unobserved
```

Local action requests, received commands and observed playback states describe
separate events. None alone identifies why playback paused.

## Diagnosing an Ogg capture rejected mid-song

Ogg capture rejections now name the failed guard: `serial-change`,
`sequence-discontinuity`, `unexpected-bos`, `invalid-granule`,
`granule-regression`, `invalid-eos-granule`, `inactive-stream`, or an invalid
page. With `LogLevel=4`, the matching bounded `Ogg observation`
records context, serial/sequence/granule expectations, flags, format, page and
byte counts, age, and queue-drop count. It never records raw audio, URI,
playback-instance, device, or credential data.

Spotify may decode crossfaded tracks concurrently. Floggfy therefore isolates
logical Ogg streams by both native sync context and Ogg serial; a BOS or page
from one serial cannot reset another valid serial on the same context. Missing,
corrupt, backward, duplicated, or ambiguous pages remain fail-closed and require
replay; Floggfy never guesses at or repairs a sequence gap.
