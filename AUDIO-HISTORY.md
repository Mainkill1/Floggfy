# To Disk audio history

The native `version.dll` provides complete-listen history and an optional legacy
connectivity repair for the Windows x64 Spotify client. It copies the compressed
Vorbis or FLAC bytes that Spotify already reads for playback. It does not request
tracks, run FFmpeg, transcode audio, or require an external runtime helper.

## Requirements and startup

- Use the Windows x64 Spotify desktop app with the x64 Floggfy files.
- Intel and AMD x64 processors are supported; PE's `AMD64` name refers to the
  architecture, not an AMD-only build. x86 and native ARM64 Spotify are unsupported.
- The GCC/C++ runtimes are statically linked. No separate Visual C++ Redistributable
  is required by Floggfy; its remaining imports are Windows system libraries.
- Launcher mode checks both adjacent binary headers before stopping existing
  Spotify processes and reports architecture/header-read errors. This does not
  validate every loader dependency or guarantee Spotify-version compatibility.
- A mismatched automatic-mode DLL cannot run its own error-reporting code.

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
NetworkPatch=0
Save Location=
Flac=1
Ogg=1
Metadata=1
Lyrics=1
UpdateExistingMetadata=1
Log=1
DebugLog=0
DebugConnect=1
DebugNetworking=0
DebugHookDiagnostics=0

[History]
MaxBufferedMiB=500
```

- **Downloads:** master switch. Start the next track from its beginning after
  enabling capture; enabling halfway through a track cannot recover earlier audio.
- **Menu:** set `0` and restart Spotify to disable menu integration. Configured
  capture and any explicitly enabled network patch operate independently.
- **NetworkPatch:** INI-only, default `0`; missing keys also disable the patch.
  Set `1` and fully restart Spotify to enable the legacy NLM compatibility hooks.
  Disabled startup skips import discovery, IAT/vtable changes and hook retries;
  audio capture, metadata and menu startup remain independent. The option is
  loaded once at startup; no live detachment of COM hooks is attempted. Missing
  keys are added through the existing atomic settings writer, preserving other
  INI content. Ordinary menu saves preserve existing restart-only entries
  (`NetworkPatch`, `DebugConnect`, `DebugNetworking`, `DebugHookDiagnostics`),
  including edits made after startup; only missing keys receive defaults.
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
- **Log:** default `1`; the master switch for both logs and all optional diagnostics.
  `Floggfy.log` contains normal activity, warnings and actionable failures.
- **DebugLog:** default `0`; requires `Log=1`. Optional playback, metadata and
  troubleshooting detail goes only to `Floggfy-diagnostics.log`.
- **DebugConnect:** default `1`; requires `Log=1` and `DebugLog=1`. Enables cached
  Connect target observations and passive local action requests. `0` opts out of
  those observations even with deep networking enabled. No discovery, playback
  transfer or extra network request is performed. Basic playback identification
  remains independent of this diagnostic option and of `Metadata`.
- **DebugNetworking:** default `0`; requires `Log=1` and `DebugLog=1`. Enables the
  read-only decoded Connect observer and allowlisted enrichment I/O observations
  in the same diagnostics file. No raw payloads, media, credentials, URLs or
  real device identifiers are written. Receipt is distinct from command execution;
  unobserved traffic and initiators remain unknown. This optional decoded-message
  hook is independent of `NetworkPatch` and the `DebugConnect` cached-state option.
- **DebugHookDiagnostics:** default `0`; requires `DebugNetworking=1`, `Log=1`
  and `DebugLog=1`. Enables callback and queue-lock profiling, interval timing
  drains, slow/pending menu and startup spans, and capture-worker progress checks.
  Basic debug mode performs no per-callback profiling. Completed spans at least
  10 ms, pending spans after 250 ms, and worker progress gaps after 1 s are sampled
  with bounded repeat reporting. These observations do not establish a crash or
  change capture acceptance. There are no dumps or additional suspension hooks.
  Launcher Windows Debug APIs remain required for startup/DLL-load synchronization.

All debug keys retain their defaults and require a full Spotify restart after
INI edits. `NetworkPatch` remains a separate startup option.

| Effective settings | Normal file | Diagnostics file | Optional callback profiling |
| --- | --- | --- | --- |
| `Log=0` (any debug keys) | Off | Off | Off |
| `Log=1`, `DebugLog=0` | Activity, warnings, failures | Off | Off |
| `Log=1`, `DebugLog=1` | Same normal events | Playback and metadata detail | Off |
| Above + `DebugConnect=1` | Same normal events | Adds cached Connect/local action observations | Off |
| Above + `DebugNetworking=1` | Same normal events | Adds decoded command receipts and enrichment I/O | Off |
| Above + `DebugHookDiagnostics=1` | Same normal events | Adds timing, slow/pending activity and progress observations | On |

`DebugConnect=0` always disables its cached Connect/action observations;
`DebugHookDiagnostics=0` always disables callback profiling. An unavailable save
root is an operational failure, but cannot guarantee disk delivery into that same
unavailable destination. Each current output has a 5 MiB cap. Normal and optional
records use separate bounded 128-entry queues, so diagnostic saturation cannot use
normal queue slots. One worker drains bounded batches fairly. Records retain their
enqueue-time timestamps; per-queue omission summaries report contention/overflow
without globally deduplicating real events.

- **MaxBufferedMiB:** 8–512 MiB, default 500, limits compressed capture allocations.
  There is also an approximately 8 MiB event queue, artwork, and a temporary
  completed-file buffer during native tagging. Oversized tracks are discarded.

The four restart-only switches above accept only an unquoted `1` (surrounding
spaces/tabs are allowed). `0`, blank, malformed and duplicate entries disable
the corresponding observation/patch; invalid existing entries are not rewritten
by menu saves. Missing keys use their documented defaults (`DebugConnect=1`,
the other three `0`). Changes still require a full restart. Preservation applies
to the INI text read for that save; simultaneous writes from an external editor
are not a cross-process transaction.

## Files and duplicates

Default music paths are:

```text
<Windows Music>\Spotify\Artists\Artist\Album\Title.ogg
<Windows Music>\Spotify\Artists\Artist\Album\Title.flac
```

Album folders have no year prefix. New filenames use the title alone by default.
`TrackNumberInFilename=1` adds `NN - ` when a track number is known; disc numbers
remain in metadata. Existing title-only, numbered or disc-prefixed filenames
are recognized and reused; switching naming mode never authorizes a second copy.
Title, artist, album, album artist and track/disc numbers are embedded
in the single audio file.
Validated cover artwork is included when available; missing artwork does not
discard completed audio. No image/JSON
sidecars or per-song directories are created. Buffers remain in RAM until a
complete listen and complete stream have been verified. Directories are created
only when publishing a file.

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
FLAC/Ogg/M4A names and legacy aliases against those lists in memory. Only listed
matching candidates are opened for identity/quality validation; absent generated
names do not cause individual filesystem probes. The list is discarded after
the operation.

The check uses one coalesced pending request on the existing publication worker;
rapid track changes discard superseded requests and audio saves take priority.
There is no library-wide sweep or capture/menu-thread filesystem I/O. A completed
listen also provides a conversion fallback if its start check could not run.
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

Renderer console diagnostics use a dedicated bounded entry point. Reserved
`FLOGGFY_STATUS:` / `FLOGGFY_NETWORK_V1:` messages are consumed even when rejected,
and validated before log routing or native failure-keyword classification.
Known client status fields use the same allowlist with deep networking on or off;
retired readiness messages remain reserved and are consumed without output.
Unknown/malformed/oversized, embedded-NUL and non-ASCII renderer statuses are
dropped. Direct generic logger calls with these reserved prefixes also use the
same sanitizer, so failure words cannot copy a rejected payload into activity
logs. Explicit enrichment request summaries still require deep mode. Native
capture failures continue to log normally when `DebugLog=0` and `Log=1`.

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
Basic `DebugLog` does not measure native callbacks, queue-lock times or empty FLAC
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
FLAC, Ogg or M4A file exists. When enabled, it opens the containing folder. A separate
worker checks destination paths; capture processing and menu rendering only
exchange bounded requests and memory snapshots. Superseded results cannot enable
a different track or save location, and `Menu=0` disables destination probes.
The shortcut first checks its 16-entry verified-file cache with a fresh file
signature. Unchanged saved files need no directory listing or audio reread.
Discovery lists the directory once and compares the three ordinary names before
legacy/collision aliases, stopping at the first complete identity match.
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
Every completed listen fixes a matching unfinished capture's deadline at ten
seconds after completion, including the same track paused at its duration. A
provider switch restarts only current listening coverage: previously completed
listens and their existing decoder-tail deadlines survive. Late-discovered
captures inherit those deadlines; repeated paused polls and later observations
cannot extend them. A capture finished after its deadline cannot be published.
Already completed audio that arrived on time may still use the separate ten-minute
reconciliation window. Completeness, duration, identity and unique-association
checks remain required, and the five-song/memory limits still apply.
Existing-file inspection uses checked reads with a separate temporary buffer
capped at 512 MiB; storage errors reject the inspection without stalling capture.
Spotify's cached PlayerAPI is the primary playback source. Track identity uses the
Spotify URI and playback instance rather than a Windows title/artist snapshot. Progress comes from the
app's position-at-timestamp, timestamp and pause state, following the model used
by [Spicetify's Player wrapper](https://github.com/spicetify/cli/blob/main/src/jsHelper/spicetifyWrapper/core.js).
The native heartbeat requests `PlayerAPI.getState()` roughly every 500 ms through
the renderer. This reads the app's cached state without a Web API request; it
continues working when the optional update-event listener is unavailable.
PlayerAPI update events preserve short transitions between the native heartbeat
polls. A bounded queue retains those observations in order; overflow invalidates
coverage rather than silently dropping a buffering or remote-playback guard.
A buffering transition carries the app's event timestamp, so time spent buffering
cannot manufacture the missing tail of a listen. Spicetify does not need to be installed. Basic playback observation remains active
with `Metadata=0`; that setting controls optional enrichment only.

The app source requires a confirmed local playback device. Remote playback and
buffering invalidate coverage instead of falling through to a possibly stale
Windows session. Renderer snapshots are bounded, strictly parsed and rejected
when older than three seconds. Clock differences up to 250 ms are tolerated
without moving an observation into the future. For the same URI and playback
instance, fresh Ready samples with nondecreasing renderer timestamps cannot
regress solely through UTC/uptime rounding within that 250 ms bound: their mapped
time is held at the preceding observation, adding no elapsed coverage. Actual
source-clock regressions, identity changes, larger discontinuities and transition
event times retain the existing guards. Seeks, missing observations and
source changes cannot be stitched into a complete listen.

When app observations are unavailable or incompatible, public Windows media
sessions provide a fallback on a dedicated observer thread. An empty first
Spotify session no longer masks another usable session, and a disconnected
session manager is recreated. Windows fallback requires Spotify's **Settings >
Display > Show desktop overlay when using media keys** to be enabled.
Normal position/timestamp refreshes are accepted when movement agrees with
uninterrupted playback, allowing 250 ms of rounding difference and clamping at
the track end. Source/session changes, discontinuous jumps, regressing clocks
and observation gaps invalidate coverage. Capture consumes sequenced RAM
snapshots without waiting for COM, disk, enrichment or network work. Stopping an
observer does not wait for a stalled COM operation. No Windows artwork is fetched.

With `Log=1` and `DebugLog=1`, `media-session` diagnostics report `source=spotify`
or `source=windows`, the failed stage, reason, HRESULT, session/candidate counts,
and numeric timeline values. Persistent failures are summarized at most every
30 seconds; recovery and source changes are reported immediately. These summaries
contain no track titles, URIs, device identifiers or raw renderer payloads.
The ordinary activity log warns when playback verification remains unavailable
for three seconds during active capture; brief app-start buffering stays in
diagnostics. This grace period affects reporting only: coverage invalidation is
immediate. Completed-only captures have a separate final failure outcome when
they cannot be saved.
Saved files include `HISTORY_PLAYBACK_SOURCE` so validation can identify the source.
A five-song history retains app identities, playback instances, timing, completion
state and refreshed labels. Up to five completed compressed streams remain in RAM
for matching, within `MaxBufferedMiB`; unmatched records expire after ten minutes.
Matching must be unique in both directions across streams and completed listens.
Ambiguous matches stay rejected after expiry or eviction; removing an alternative
is never treated as evidence of ownership. Observation loss under lock contention
also invalidates coverage instead of losing a guard silently.
Incomplete decoder tails retain their original ten-second deadline. Audio is
published as a final file only after complete-listen and integrity checks pass.
Destination resolution returns a verified matching-file snapshot or an explicit
vacant/blocked outcome. Publication reuses that snapshot for quality and missing
metadata decisions; replacement still revalidates the locked object. The bounded
collision search still examines occupied deterministic names to retain the best
matching recording, rather than scanning directories or maintaining another cache.

## Troubleshooting

1. Quit Spotify and set **Metadata=0**, then restart. This isolates client metadata
   service/cache discovery changes while retaining basic Windows media tags and
   app playback observations. Audio still saves without a cover.
2. If menu integration crashes or disappears, set **Menu=0** and restart. Configure
   Downloads, FLAC and Ogg directly in the INI.
3. Enable **Log=1** and, temporarily, **DebugLog=1**. Inspect `Floggfy.log` in the
   configured save root. Check `media-session` lines for the selected source,
   failure stage, reason and HRESULT. `source=spotify` works independently of
   Windows media controls. For `source=windows` with `no-spotify-session`, enable
   Spotify's **Settings > Display > Show desktop overlay when using media keys**.
   Replay a fresh track from its beginning after observation recovers. Turn
   debug logging off afterward.
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
uses the existing bounded capture/publication workers; it adds no codec-specific worker or menu
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

Run `bash test-diagnostics.sh [SPOTIFY_DLL]` from WSL with the MinGW toolchain and
Windows executable interop available. It covers explicit categories, queue/file
isolation and saturation, renderer privacy and opt-outs, capped-log I/O counts,
read-only decoded command handling and aggregation, basic/profiling callback work,
format-detour removal, playback/quality guards, and exact embedded INI defaults.
The optional DLL is mapped read-only for current-build discovery; it is not
executed. Omit it to run synthetic discovery with an explicit skip of that check.
Native portable regressions remain `bash test-native.sh`; the full Windows and
startup/publication regressions remain `bash test-native-windows.sh`.

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

After, optional `Floggfy-diagnostics.log` adds facts rather than the save outcome:

```text
... source=spotify ... pos=0.018/231.000 ...
... FLAC observation ... epoch=2 ... queue_lag_ms=0.5 ...
... direction=local source=client-control request=pause source=local-client
... direction=in service=connect ... action=pause ... execution=unobserved
... direction=local source=client-state paused=1 buf=0 ...
```

Local action requests, received commands and observed playback states describe
separate events. None alone identifies why playback paused.
