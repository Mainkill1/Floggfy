## Release 1.2.4 handoff

The maintainer approved merge and release after controlled use on Windows x64
Spotify 1.3.6.1921. Release packaging uses the unchanged native/collector code at
`dd5c24b`, plus the packaging version and bundled-license update. The installed
DLL SHA256 is `ce3f9bf8e7cf31214229649c11f1b2e06afbfb4e490fb8e1dae54e9ea03e731b`.
Its main-process module path, startup-ready signal, visible window and unchanged
INI were verified after installation. That is deployment evidence; it does not
turn the failed/unexecuted regression cases below into passing tests.

Packaging verification checked that the standalone DLL and ZIP core match that
build, the launcher matches its built executable, internal/external checksum
manifests match, and the archive includes the shipped INI, README and all six
bundled-library license notices. No new behavioral tests, reruns or full suites
were executed for release. The album-artist, full directory-lookup and filename
option runtime coverage limitations remain recorded below.

Known follow-up: a discovery miss currently lists both known artist folders
upfront; the separate optional conversion check may reread an already unnumbered
file. Cached Song hits only check their existing file signature. The Song link
opens the actual verified recording, including a retained legacy folder. Release
1.2.4 does not relocate existing music or delete already-created duplicates.

The sections below retain the validation state at each change's test attempt.

## Album artist and contributing artists

The collector and native cache previously preferred longer name lists for both
ARTIST and ALBUMARTIST. Album artist is now an identity choice, not a completeness
score: current explicit album artists override older cached lists, and the native
cache only fills a missing ALBUMARTIST. Full contributing credits can still enrich
ARTIST. Multiple explicitly reported album artists are retained by the collector.
The production bridge honors a known Windows album artist for catalog identity,
placement and emitted ALBUMARTIST, keeping guest performers in ARTIST. A bounded
lookup-only legacy credit string retains discovery of the one known older folder;
it cannot name a new file or alter tags. Request identity includes that alias.
No punctuation conversion, library sweep, worker or metadata-rewrite policy was
added. Existing populated file tags are not automatically replaced.

Validation is **incomplete**. Exactly one `bash test-album-artists.sh` invocation
failed (exit 1): a missing closing brace in the JavaScript fixture caused a syntax
error before any assertions ran. The Windows executable compiled and linked with
`-Wall -Wextra -Werror` before the Node step, but was not executed. The fixture now
uses a separate registry declaration and a simpler object literal. No runtime
test was rerun under the one-run limit.

Independent static review approved the policy and legacy-lookup guards. The
unexecuted JavaScript cases cover explicit/current versus longer cached album
credits, separate ARTIST guests and multiple genuine album artists. The unexecuted
Windows cases use the actual metadata bridge and cache merge, checking tag/catalog
precedence, missing-field behavior, legacy-folder discovery/rename/reuse and
Metadata=0. Storage enrichment and artwork providers are stubbed; no workers or
live client requests are started by that executable. No full suite ran or saved
user recordings were changed. The installed DLL remains the preceding `5d2c6cf`
build; this policy correction has not been deployed.

One final compile-only operation passed (exit 0): `node --check` for the corrected
fixture and production collector, then `bash build-native.sh` for the DLL/launcher.
No runtime assertions were executed by those checks. Existing vendor warnings
remain visible. This is compilation evidence, not a passing test result.

## Song cache discovery throttling

The Song worker checks a matching verified path/signature before listing its
folder. Repeated unchanged saved-song requests avoid enumeration and full audio
reads. A changed/deleted cached file immediately falls through to directory-first
discovery. One unavailable request/timestamp in the existing worker cache backs
off empty or failed discoveries for five seconds. Full request identity/settings
and publication revision changes bypass the cooldown. Explicit successful-save
refresh still forces fresh discovery; no extra worker or directory index exists.

A live conversion failure also exposed artist-folder drift: an existing numbered
recording and a new capture had identical Spotify URI, sample count, bit depth and
sample rate, but enriched album-artist text sent lookup/publication to a different
folder. No user recording was renamed/deleted during inspection. A known Windows
album artist now anchors placement separately from full Spotify artist tags. The
one exact enriched-artist directory remains a lookup-only alias, so files already
saved there remain discoverable and best-quality selection spans both locations.
Each known folder is listed once, with no library sweep; conversion retains the
selected recording's current folder and only removes the filename prefix.

`bash test-directory-lookup.sh --song-cache` — **PASS**, exit 0, exactly one
combined execution for these two findings (one allocation each). It compiled with
`-Wall -Wextra -Werror`; vendor warnings remain unsuppressed. Real Windows TEMP
fixtures verified zero directory/full-audio reads on unchanged positive cache
hits, immediate changed-signature rediscovery, zero I/O during the miss cooldown,
expiry discovery, explicit positive/negative refresh, new tracks/settings, failed
listing backoff and stale-request rejection. Artist-folder cases renamed a legacy
file despite richer tag artists, retained full identity tags, found and renamed
an existing higher-quality file in the enriched-artist folder, and skipped a
lower-quality replay rather than creating a file in the vacant stable folder.
Each of the two exact folders was enumerated once per discovery operation.

Independent static review approved both changes before the focused execution.
The actual metadata bridge is replaced by an enrichment stub in this test; it is
not a live Spotify metadata test. Earlier failed full directory and filename-option
runs below remain historical: their other runtime cases were not rerun. No full
suite ran, no user recordings were modified, and the installed DLL is unchanged.
One standalone `bash build-native.sh` passed (exit 0), compiling the actual
metadata bridge and producing the DLL/launcher. No additional tests or test
reruns ran after the focused pass. Live Spotify validation of this new DLL remains
pending; existing recordings and the installed DLL were left intact.

## Directory-first recording lookup

At `34218e3`, conversion checks, publication lookup and the Song worker listed
the containing song directory once per operation and compared filenames in memory.
The Song worker now reaches that listing only when discovery is needed, as above. The
same short-lived list serves FLAC, Ogg and M4A and all deterministic legacy or
collision aliases. Only listed candidates/backups are inspected. Existing best
quality and identity rules remain; this is not a library sweep or directory cache.
Windows case-insensitive comparison is used. Incomplete/unreadable listings and
more than 16,384 entries block the operation rather than imply a vacancy. Final
renames/publication retain live handle locking, identity checks and no-overwrite
promotion. No filesystem work moved onto capture or menu callbacks.

Validation is **incomplete**. Exactly one `bash test-directory-lookup.sh` execution
failed compilation (exit 1) before runtime cases: the Windows `wmain` test entry
point lacked an explicit return. It now returns 0. The test was not rerun.

One subsequent standalone compilation check passed (exit 0): the corrected test
source compiled with `-fsyntax-only -Wall -Wextra -Werror`, followed by
`bash build-native.sh`, which produced the DLL and launcher. Neither step executed
the test. Existing vendor warnings remain visible. Independent static review
approved the directory/identity/recovery and mutation guards before the attempted
test. No full suite, runtime fixture cases or live Spotify checks ran for this
follow-up, and the installed Spotify DLL was not replaced.

The unexecuted cases assert one listing across codec lookups, no absent-candidate
attribute probes, best collision reuse after deletion, case-insensitive names,
locked files, backup recovery, directory occupants, missing directories, failed or
partial enumeration, entry bounds, Ogg/M4A prefix conversion, occupied targets,
menu cache invalidation and preferred-format publication. They are written, not
claimed as passing. Earlier validation below describes preceding revisions.

## Configurable filename numbering and track-start conversion

Added INI-only, default-off TrackNumberInFilename and ConvertNumberedFilenames.
Numbering selects NN - Title versus Title for new files; disc information remains
in tags. Both enabled flags are normalized to numbering on/conversion off, and the
conversion value is persisted as 0 through the existing atomic Unicode settings
writer. Missing keys are added without discarding unknown settings.

When conversion is enabled, a playing track observation queues one coalesced
request on the existing publication worker. Audio jobs keep their two queue slots
and have priority. The producer does no disk I/O and invalidates older requests
even when its try-lock loses; unchanged requests do not starve an in-flight check.
Identity deduplication uses raw catalog fields rather than sanitized filenames.
The check selects the best verified saved recording and converts an exact known
old prefix. A completed-listen conversion fallback uses the same policy.

The converter locks a complete owned file against writes/deletes, verifies its
recording identity and performs a no-overwrite same-directory handle rename.
Source/configuration permission is checked again after inspection immediately
before the rename. Audio, tags, artwork and lyrics are not rewritten. An existing
Latest Downloads row follows the new path without creating a download entry.
Occupied targets, ambiguous/unowned files and lost requests do not authorize an
overwrite or duplicate deletion.

Validation status for this addition is **incomplete**:

- One execution of bash test-filename-options.sh failed compilation (exit 1),
  before any runtime cases ran. The raw-identity pointer initializer mixed const
  and non-const string pointers; the source now uses an explicitly typed const
  pointer array. The focused test was not rerun, respecting the one-run limit.
- One subsequent standalone bash build-native.sh passed (exit 0), compiling the
  corrected production source and producing the DLL/launcher. Vendor warnings
  remain unsuppressed; the production C++ build retains -Werror.
- Independent static review approved the corrected cancellation, identity and
  rename guards. Static review is not runtime validation.

The unexecuted regression cases cover all option combinations, startup conflict
persistence and Unicode/unknown settings, byte-preserving FLAC/Ogg/M4A renames,
occupied/locked/unowned sources, genuine numeric titles, legacy identity suffixes,
track-start processing without a completed listen, lost-lock/superseding requests,
late settings cancellation, independent audio queue capacity, recent-row remapping,
and numbering-mode save/menu behavior. None is claimed as passing in this addition.
Both new options ship disabled. No full suite or fuzzing ran. After installing
the built DLL, the user explicitly enabled conversion, restarted Spotify and
reported that it works. The installed module, startup-ready signal and configured
option values were observed. This is a user-reported live functional check, not
a pass of the automated regression cases or an independent before/after audio-byte
comparison on the music library. The failed focused run remains recorded above.

## Song shortcut and filename compatibility follow-up

New files use the song title alone; track/disc numbers remain in tags. The shared
bounded naming helper recognizes older numbered, disc-prefixed and suffixed names
before choosing a new destination. Historical aliases are lookup-only: a vacancy
at an old numbered name cannot become a new output. Existing matching recordings
are reused in place, preserving audio-quality/identity/recovery decisions. Genuine
different-recording collisions still preserve both recordings safely.

The folder shortcut checks the three ordinary filenames before any collision
lookup and stops at the first complete identity match. It does not invoke the
publication resolver or choose the best audio rendition merely to open a folder.
A normal matching file needs one complete inspection, rather than resolution plus
a second inspection. The existing worker cache now retains at most 16 positive
path/catalog/signature entries across track changes; it holds no audio/artwork.
Revisits check signatures, and changed files, request settings and successful saves
invalidate reuse. Menu callbacks remain RAM-only. An empty pending target can
become ready during submenu preparation only for its frozen identity and settings;
an already-bound label/target is never redirected.

`bash test-song-shortcut.sh` — **PASS**, exit 0, one combined Windows executable
execution covering both findings. No finding exceeded one execution. Assertions
covered zero collision searches and one inspection for ordinary FLAC/Ogg/M4A,
no additional full read on a verified song revisit, changed/missing/wrong-identity
files, blocked storage, settings/freshness cancellation, delayed menu-target
readiness and frozen click targets. Layout/resolver cases retained track/disc tags,
reused numbered/disc-prefixed and old suffix names instead of allocating another
file, and preserved unrelated same-title recordings. The existing CEF callback
microbenchmark ran inside that same executable; it is not a measurement of real
Spotify menu-opening latency.

One standalone `bash build-native.sh` also passed. Project C++ used `-Werror`;
existing vendor warnings remain visible. Independent static review approved the
changes. No test rerun or full suite ran. The validation uses synthetic fixtures,
not live Spotify CEF or a measured slow network share. First-time verification
still reads complete audio, on its storage worker. The compatibility change does
not bulk rename or delete existing music or automatically remove prior duplicates.

## Timeline-refresh follow-up to the fourth review

The observer's exact timeline equality check at `22984d0` rejected legitimate
Position/LastUpdatedTime refreshes during the asynchronous property request.
The guard now compares movement at the two source timestamps at normal 1x
playback, allowing up to 250 ms of rounding difference and end clamping. It still
requires the same app/canonical session, bounds and playback state, valid positions,
nonregressing timestamps and no backward movement. Discontinuous jumps and wraps
retry rather than attach potentially stale properties. This follows the Windows
[Position/LastUpdatedTime contract](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessiontimelineproperties).
The observer thread, original observation timestamps and three-second coverage
limit are unchanged; capture never waits on COM.

`bash test-media-observer.sh` — **PASS**, exit 0, **one execution**. The focused
Windows executable compiled production media code with `-Werror` and exercised
the shared guard with repeated position/time changes across six simulated seconds
while the production listen tracker remained eligible. Forward/backward seeks,
wraps, duration/origin/status changes, paused timestamp refresh, clock regression,
invalid positions and end clamping were covered. A genuine four-second observation
gap still invalidated coverage. Existing injected blocked-provider, snapshot
freshness/sequence and stop-lifetime cases also passed in that same invocation.
Independent static review found no concrete blocker.

One standalone `bash build-native.sh` execution also passed, producing the DLL and
launcher; vendor warnings remain visible. No test rerun, full suite, live SMTC or
Spotify playback was performed for this follow-up. The running Spotify DLL has
not been replaced. These checks establish the guard's behavior with constructed
timelines; real application timeline precision and session transitions still need
live validation.

## Fourth review: media I/O, preferred recordings and AAC memory

Source revision `55c2be2` (documentation-only changes may follow). A dedicated
basic-media observer owns Windows COM/WinRT calls and waits. Capture consumes
sequenced RAM snapshots with original observation times; it never calls the
blocking media reader. Session/timeline changes retry without publishing a false
failure. Missing usable observations for more than three seconds still invalidate
coverage; the change prevents a stalled observer from blocking decoder events,
not from making unavailable coverage trustworthy. No Windows artwork provider was
reintroduced.

Preferred-format lookup now distinguishes a vacancy, a verified matching
recording and an unsafe/blocked result. Locked FLAC/Ogg candidates and ordinary or
suffixed `.previous` backups defer AAC publication instead of permitting a duplicate.
The bounded resolver returns its existing-file snapshot for skip/metadata planning,
so publication does not immediately reread the same file. Replacement still locks
and revalidates the actual object; those safety reads are intentional. Every
occupied deterministic collision candidate still needs inspection. There is no
unbounded scan or new filesystem cache.

SpAC extraction and M4A construction each enforce a conservative 512 MiB operation
peak estimate before large allocations. Extraction reserves the known packet count
and checks source, restored bytes and object overhead. Publication releases flattened
source before muxing. Construction budgets retained capacities, the full 128 MiB
vendor allowance, one reserved media output and metadata/table scratch, then rewrites
only the terminal movie metadata in place. External metadata tagging keeps its
source and budgets its required second buffer. Resource failures are distinct from
invalid/unsupported inputs. This is an operation bound, not measured process-wide
peak memory or an independent audio decode.

Targeted command: `bash test-aac.sh <complete-spac-file> <spotify-dll>` — **PASS**,
exit 0, **one execution total**, shared by all three review findings. No finding
exceeded one test execution. The Windows executable covered:

- A provider blocked for over 3.1 seconds while RAM-only capture-style polling
  continued; one-time sequence consumption, stale rejection, new identities,
  failure publication and nonblocking stop/restart lifetime.
- Actual Windows sharing locks on FLAC, Ogg, collision suffixes and recovery
  backups; no new M4A while uncertain, retained bytes and recovery after unlock.
- Allocation-free peak arithmetic at the boundary and beyond it, overflow/count
  rejection, actual resource-failure classification, preserved metadata/artwork,
  exact compressed media/configuration/timing and unchanged no-op behavior.
- The real HE-AACv2 source: 4,063 access units, 566,143 media bytes, 599,576-byte
  M4A. Every output media packet matched its input byte for byte. Optional hook
  discovery was checked against the supplied Spotify client without invoking it.

Project C++ compiled with `-Werror`; existing vendor warnings were unsuppressed.
One standalone `bash build-native.sh` execution also passed (exit 0), producing
the production DLL and launcher. These corrections have not been installed into
the running Spotify instance.
No test rerun, full suite, fuzzing, independent decoder invocation or live Spotify
playback was performed for these corrections. The media provider test uses an
injected callback, not SMTC: real Windows session/timeline coherence and live
transitions remain unverified on this revision. Slow storage can still occupy the
publication worker and exhaust its bounded queue. The lookup change removes
redundant planning reads; it does not promise cancellation of stalled file I/O.
Earlier VLC decoding/live captures below are historical evidence for their stated
revisions, including the documented VLC trimming variance.

## Optional SpAC AAC capture

`AAC=0` is the new default and the option is INI-only. Supported full SpAC AAC
streams are captured through the existing bounded workers and remuxed as M4A
without re-encoding. Unknown configurations, partial/seek streams and frame gaps
are rejected. Existing matching FLAC/Ogg recordings suppress a low-quality AAC
counterpart; complete AAC recordings can receive metadata-only updates when
allowed. AAC VBR size differences do not authorize an audio-quality replacement.

Forensic inspection of an actual low-quality playback found a complete HE-AACv2
stream: 371 checksum-valid SpAC pages, 4,063 access units, ASC `eb8a0800`, rendered
44,100 Hz stereo, 8,314,226 presented samples, 5,119 priming samples and 1,679 trailing
samples. It contains no Ogg/Vorbis markers. These are actual encoded packets;
synthetic test packets are labeled as such and are never claimed to decode.

One combined `bash test-aac.sh` invocation **passed** (exit 0), exercising parser,
M4A container/tagging, disabled-default and Downloads gates, native hook forwarding,
complete publication, duplicates, metadata opt-out/fills, preferred FLAC retention,
recording identity and collision names. Optional private arguments remux the
actual captured stream and map the supplied Spotify DLL for static discovery.
The gate does not invoke Spotify functions or play synthetic AAC packets.
The actual stream was remuxed to a 599,576-byte M4A. Its 566,143-byte `mdat`
payload hashes identically to the original extracted access units. A separate
VLC/FAAD decode succeeded as HE-AACv2/SBR+PS, 44,100 Hz stereo with no decoder
errors. VLC produced 8,314,880 PCM frames, 654 more than the 8,314,226 presented
samples in the source/container edit timeline (about 14.8 ms). The container
encodes source timing exactly; this check does not claim sample-exact VLC trimming
or equality to Spotify's rendered PCM. Synthetic sentinels were not decoded.
Vendor warnings remain visible; project C++ compiled with `-Werror`.
The production DLL/launcher build passed. With the optional AAC setting enabled,
Windows Spotify 1.3.6.1921 completed a live capture and published a 703,075-byte M4A
through the normal recording pipeline. The file contains a 640 by 640 validated
Spotify cover, 3,521 bytes of synchronized lyrics and recording-identity metadata.
Its AAC payload still hashes identically to the independently inspected source
access units. Queue diagnostics showed no dropped events. The private observation
module was removed before this production replay; only the production hooks were
loaded. This establishes one successful full AAC publication on that client,
not support for every Spotify build/profile or independent PCM equivalence.
No full suite has been run for this addition. Prior evidence below applies only
to the revisions explicitly described there.

## Late capture completion after a playback transition

Live testing reported repeated completed-listen association failures during
low-quality playback. The previous activity message conflated missing and
ambiguous matches and labeled both FLAC and Ogg failures as Ogg. That historical
log does not establish the exact cause of the reported song's failures.

Static investigation found a concrete ordering race: a natural title transition
queued the completed listen, then deleted its still-active capture before the
final codec callback could arrive. Pending-listen candidates now survive only
within the original ten-second completion deadline and the existing birth and
generation bounds. Stream completeness, duration, uniqueness, epoch and integrity
checks remain required. Skips still discard unassociated captures, and retained
buffers are released when their listen is handled or the deadline expires.
Failure entries now include the track, separate missing/ambiguous reasons, and
ready/active timing and duration counts even when DebugLog is disabled.

Targeted validation: `bash test-capture-association.sh` — **PASS**, one execution
(exit status 0). Project C++ compiled with `-Werror`; the vendor parser emitted
two unsuppressed unused-function warnings.
The test injects decoder events and timeline observations into production capture
and scheduling functions; it does not play Spotify or independently decode audio.
Coverage includes late Ogg EOS/FLAC frames, normal changes and repeat-one, invalid
or expired captures, duration mismatches, ambiguous matches, ordinary skips and
pending-ready preservation across a later short-track transition. No full suites
are run for this fix. Live confirmation of the reported failure remains pending.

## Windows artwork fallback removal (after 96a2768)

Windows thumbnail acquisition, its worker/cache, retry helper and artwork fields
in media-session snapshots have been removed. Basic Windows track/timeline reads
remain; Spotify is the only active external artwork source. New saves can be
coverless. Existing artwork, synchronized lyrics and custom tags continue through
eligible audio upgrades, even with `Metadata=0`. Metadata-only artwork fills and
upgrades still honor `UpdateExistingMetadata=0` and preserve compressed audio.

Validation for this change is limited to one targeted command:
`bash test-artwork-policy.sh` — **PASS**, one execution, exit status 0.
The vendor parser emitted two unsuppressed unused-function warnings; project
C++ compiled with `-Werror`. No code changed after this execution.
The focused Windows executable compiles/links the capture and media-session code,
but exercises enrichment through an injected Spotify artwork-cache boundary.
Its cases cover valid/invalid/unmatched client art, coverless FLAC/Ogg publication,
missing/better artwork updates, opt-out and unchanged updates, and preservation
of an existing cover, synced lyrics and custom fields in a 16-to-24-bit FLAC
replacement without new artwork. No full suites or additional builds are run.

Live Spotify capture, SMTC/WinRT track and timeline reads, real CDN fetching,
menu behavior and interrupted-process recovery are not exercised by this test.
Source review confirms the corresponding recording safeguards and Spotify artwork
fetch implementation were retained; this is not a claim of live Windows coverage.

All results below predate the fallback removal. The historical thumbnail-cache
case and correction-runner issue 2 were retired with their implementation.

## Historical PR #11 review corrections (through 96a2768)

The original correction pass used exactly one targeted execution per issue.
After its settings failure, a separately authorized follow-up compiled and ran
issue 5 once more with a deterministic redundant-notification check: it passed.
Total: seven executions, two for issue 5 and one for each other issue. No full
suites, fuzz/stress runs or live Spotify checks ran in either pass.

| Issue | Command | Executions | Result |
| --- | --- | --- | --- |
| 1: artwork/tag preservation during upgrades | `bash test-pr11-corrections.sh 1` | 1 | Pass (Windows) |
| 2: asynchronous thumbnail retention | `bash test-pr11-corrections.sh 2` | 1 | Pass (Windows, injected media provider) |
| 3: collision destination reuse | `bash test-pr11-corrections.sh 3` | 1 | Pass (Windows) |
| 4: delayed-title/repeat reconciliation | `bash test-pr11-corrections.sh 4` | 1 | Pass (portable) |
| 5: settings persistence failures | `bash test-pr11-corrections.sh 5` | 2 | Original cleanup assertion failed; authorized follow-up passed (Windows) |
| 6: Vorbis setup-header validation | `bash test-pr11-corrections.sh 6` | 1 | Pass (portable) |

Issue 5 reached its final cleanup assertion after the exception, denied-write,
read-failure, retry, Unicode and unknown-section assertions succeeded. Inspection
found that a pending notification could redundantly persist a generation already
acknowledged by `FlushSettings`, exposing a staging file after the flush returned.
No leftover staging file remained on later read-only inspection. The worker now
skips already-persisted generations while retaining retries for failed generations.
The authorized follow-up compiles the actual persistence worker and deliberately
signals it again after a successful flush. It waits for that notification to be
fully processed, then verifies no additional INI read occurred, file bytes remain
unchanged, flush still succeeds and no staging file remains. All original failure,
retry, Unicode and unknown-section checks also passed. This validates the final
settings guard without relying only on the timing of the original failure.

One standalone `bash build-native.sh` invocation built the DLL and launcher
successfully before the targeted tests and final settings correction. The vendor
parser emitted two unsuppressed unused-function warnings. This build therefore
does not include the final settings edit; the follow-up Windows test compiled
and executed that edit directly. Project C++ checks retain `-Werror`;
no warnings were suppressed to pass a test.

Coverage includes missing/invalid/inferior/better artwork; repeated custom tags
and synchronized lyrics; unchanged incoming compressed audio, identity checks and
locked publication; retained A artwork after B/C transitions, bounded coalescing,
stale identities and `Metadata=0`; deletion of an ordinary collision occupant and
reuse of its higher-quality suffix; equal/different-duration delayed titles,
repeated cycles, seeks and observation gaps; malformed/truncated Vorbis setup,
unsupported headers, CRC corruption and missing EOS.

Windows media-session/WinRT thumbnail retrieval was not exercised against Spotify
in this pass. Cache tests inject the provider and use actual Windows threads.
Real SMB outages and interruption during publication were not retested. Vorbis
validation checks supported headers separately from structural Ogg CRC/framing/EOS;
it does not independently decode every audio packet. Unsupported headers are
rejected conservatively and cannot authorize replacement. Audio upgrades retain
bounded temporary old/new metadata buffers; the 512 MiB file-read limit is not
a total process-memory limit.

## Historical recording integrity and metadata-update candidate (e8ae8f8)

The following results predate these corrections and were not rerun on the updated
candidate.

The unchanged `bash test-native.sh` and `bash test-native-windows.sh` commands
pass on the combined candidate. The Windows suite also builds and tests the DLL
and launcher. GCC 14 with `_FORTIFY_SOURCE=2`, `-O2` and `-Werror` passes the FLAC
test that previously ignored `fread` results. No warning suppression is required.
Independent source review found no unresolved findings after its corrections.

Regression coverage includes:

- Storage blocked for more than three seconds while capture queue production,
  draining and listen observations continue; stale requests and `Menu=0` handling.
- Complete-file and recording-identity checks, disc/name collisions, owned partial
  recovery, conflicting writers, locked files and actual publisher process
  termination during first saves and upgrades.
- Repeat-one generations, seeks, delayed titles, pauses and invalidated coverage.
- Unicode INI migration and fresh-process reload, failed migration preservation,
  large legacy files, concurrent persistence and exception-safe lock ownership.
- Metadata heartbeat freshness during blocked enrichment and retained rich fields.
- Missing artwork and metadata fills for FLAC/Ogg, unchanged compressed audio,
  preserved custom/repeated fields and padding, date/year conflicts, synchronized
  lyrics, metadata-only race protection and unchanged second passes.
- Existing-metadata opt-out persistence, generation changes during updates,
  no-op bypasses, identical images, trusted client-cover upgrades, larger-cover
  preservation and unsupported/oversized-image preservation for FLAC/Ogg.
- Checked read failures, short reads, size changes, allocation failures, frozen
  menu targets, reference cleanup, and the **Latest Downloads** label.

Live checks used Windows x64 Spotify **1.3.6.1921** and an isolated output folder.
Repeat-one produced a complete FLAC on the first cycle. After optional date/year,
lyrics and artwork were removed from that test copy, the next complete cycle
restored them without changing the compressed audio bytes. The restored FLAC
passes independent Xiph decoding, frame CRC and STREAMINFO audio MD5 checks.
The menu hid Latest Downloads before a save and enabled the Song folder shortcut
only for verified saved audio; its final submenu label is Latest Downloads.

On the final code candidate, a separate complete replay with
`UpdateExistingMetadata=0` preserved the seeded existing file byte-for-byte and
kept its modification time. Enabling the option restored missing date/year and
lyrics and upgraded a deliberately small 1 × 1 PNG cover to the validated
640 × 640 Spotify JPEG without changing the compressed audio.

A further complete repeat preserved both file bytes and modification time,
confirming an unchanged replay does not rewrite the file. The final enriched
FLAC also passes independent Xiph decoding, frame CRC and supplied STREAMINFO
MD5 checks with artwork required.

The final DLL also saved a complete 320 kbps nominal, 44.1 kHz stereo Ogg
recording (9,177,757 bytes). Independent Xiph Vorbis decoding and Ogg CRC checks
pass. Artwork was unavailable for this song; the audio and text tags were saved
successfully, exercising the optional-artwork path. Original DLL and INI backups
were restored byte-for-byte after live testing. All music fixtures stayed in an
isolated test location.

Slow-storage and interrupted-publication evidence comes from controlled Windows
fault/process tests, not a real SMB outage or killing Spotify mid-write. An
interrupted write can leave a `.complete-*` staging file; it is never accepted as
the final recording. Upgrade recovery may briefly leave the normal path absent
while retaining the prior audio. Unknown/unowned files are preserved. Existing-file
inspection uses a separate temporary buffer capped at 512 MiB per worker.


## Version 1.2

The portable native regression suite and Windows regression/startup suite pass.
Coverage includes new-install defaults and saved disabled preferences, menu
capability fast paths, bounded metadata requests and cancellation, exact-track
association, artwork validation/cache, embedded LRC timing, Unicode lyrics,
metadata-only FLAC updates, audio-quality upgrade protection, locked-file
preservation and oldest-line log rotation at 5 MiB.

Live checks used Windows x64 Spotify 1.2.93.667 and 1.3.6.1921. On 1.2.93.667,
the earlier candidate attached menu and metadata hooks on three fresh starts,
passed all 15 menu probes and obtained lyrics with the lyrics panel closed.
On 1.3.6.1921, both hooks attached on three fresh starts and lyrics arrived with
the panel closed. Initial menu probes passed 14 of 15 times; the first physical
click timed out for an unconfirmed reason. Five subsequent probes on the final
DLL passed, taking 43–67 ms including UI Automation lookup.

A complete-listen capture on 1.3.6.1921 contains embedded synchronized lyrics,
a 640 × 640 JPEG album cover and track/album metadata. Replaying into an existing
plain-lyrics FLAC upgraded it to 27 timestamped LRC lines (936 UTF-8 bytes).
Independent comparisons confirmed identical compressed audio, all non-comment
metadata blocks and all unrelated comment fields. Both the fresh synchronized
capture and the metadata-upgraded file pass full Xiph FLAC decoding, frame CRCs
and supplied STREAMINFO audio MD5 verification.

Spotify supplied 16-bit/44.1 kHz stereo FLAC in these recent captures. An older
24-bit capture of the same track is a different audio rendition; the audio-byte
preservation check compares a file before and after retagging, not those two
renditions. Existing higher-quality audio is not replaced by lower-quality audio.

The client service now requests track/album metadata and lyrics automatically.
Lyrics availability remains track-dependent, and internal Spotify APIs can change.
`Lyrics=0` disables lyric requests, discovery and updates; `Metadata=0` disables
all extra metadata collection. Existing-library lyric updates apply to FLAC after
a complete replay. No bulk library scan or separate LRC files are created.

## Historical validation for version 1.1.0

The notes below describe the previous cache-only collector and earlier releases;
their cache-only, missing-lyrics and log-reset statements do not describe 1.2.

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

## RC4 playback quality menu (historical layout)

The bottom of To Disk has five disabled informational rows: song, quality,
format, bitrate and sample rate (with FLAC bit depth). A worker publishes a fixed
RAM snapshot; the menu uses a nonblocking read and rejects snapshots older than
three seconds. Opening this section performs no file, artwork or endpoint reads.
Downloads-off observation uses the existing bounded queue and retains only small
headers and counters, without creating whole-song capture buffers.

The existing renderer collector reads actual current playbackQuality/
playback_quality from its local player snapshot, never targetBitrateLevel.
Known string levels and protobuf enum values 1–6 are normalized; new values
remain unavailable. The numeric enum mapping was checked in the installed
1.3.3.264 DLL and the archived 1.3.0.277, 1.2.94.583 and 1.2.92.148 DLLs.
Native use requires an exact title, artist, album and duration cache match,
candidate uniqueness and receipt age under twenty seconds. Playback quality is
not embedded into permanent song tags. Metadata=0 disables this optional source;
validated decoder headers still identify lossless quality and FLAC bit depth.

Synthetic Linux and Windows tests cover current-vs-target quality, quality
changes, missing/unknown fields, zero network/service/accessor calls, stale/wrong/
ambiguous cache identities, Ogg duration corroboration, read-ahead rejection,
same-song replay, separate title/timeline transitions, seek invalidation,
codec/quality conflicts, menu placement,
refresh, disabled-command ownership, snapshot contention and expiry. Observer
tests verify that showing quality cannot enable saving and preserves the original
Ogg parser return.

Numeric bitrate is the average encoded bitrate once the complete stream is
known, rather than read-ahead bytes divided by partially decoded time. Ogg decoder
association waits for EOS to corroborate duration. Decoder matching uses timing
and duration heuristics, not a native track ID; unknown or ambiguous sources
remain unavailable. Labels refresh on opening the submenu, rather than changing
continuously while it stays open. No estimated file size is displayed.

On installed Windows Spotify 1.3.3.264, all five disabled rows appeared in the
actual To Disk submenu. Cached Lossless quality was displayed with Downloads
off, and the audio library remained unchanged. A live observed FLAC stream
also displayed 44,100 Hz during development. Final native and Windows suites
passed, along with the playback tracker under AddressSanitizer and
UndefinedBehaviorSanitizer. Numeric bitrate was verified with synthetic complete
streams; this release does not claim an additional full real-track bitrate test.

## Current-track footer correction

The current-track heartbeat reads the existing local player snapshot once per
second. Its short message bypasses artwork reads and metadata enrichment.
CEF’s public FormatLabel callback reads a fixed-size RAM snapshot while
constructing visible labels, before MenuWillShow; it no longer includes
Bitrate. A fresh client identity takes precedence over a lagging Windows media
session. Decoder format and sample rate are retained only when identities and
quality agree. Missing or stale client state clears the footer instead of
restoring the previous song. No request endpoints are called.

Regression checks cover a client track change ahead of Windows, repeated old
Windows publications, matching decoder information, expired heartbeats,
clearing the current song, contradictory FLAC bit depth, pre-show label
formatting, and four disabled menu rows without duplicates.

Live Windows Spotify 1.3.3.264 validation confirmed that the first submenu
opening displays the visible current track. Consecutive skips were checked
after allowing two seconds for the heartbeat: the footer matched the new
visible track each time, contained four disabled rows, and had no Bitrate row.
The current observed FLAC stream displayed its validated sample rate and
bit depth. Older-client runtime behavior remains unvalidated.

## Startup fallback and localized menu

The Launcher-mode Floggfy.exe addresses System32 VERSION.dll winning the
normal module-name lookup. It stops a newly created Spotify process at its
PE entry point after loader initialization, restores the entry instruction,
detaches debugging, and loads the adjacent Floggfy.dll by absolute path.
It resolves LoadLibraryW relative to its actual owning Windows module, checks
the complete loaded DLL path, waits for worker readiness, and resumes Spotify.
Module snapshots retry bounded ERROR_BAD_LENGTH failures during loader churn.
Signed Spotify files and Windows DLL-search policy are not modified. Existing
Spotify processes from the matching executable path are force-stopped before
the new process starts. Shutdown is bounded; other installations are left alone.

The Windows startup fixture preloads System32 VERSION.dll before entry.
Normal launch demonstrably skips the adjacent DLL. The fallback loads it and
detaches its debugger. Failure checks cover absent/invalid DLLs, an unavailable
readiness signal, and rejection of mixed automatic/launcher installations.
Restart checks cover multiple matching processes and preservation of a process
from a different installation path.

Menu discovery already uses public CEF item types rather than translated
File/Edit/View labels. Regression models use Japanese, German mnemonics and
Arabic labels. Live Japanese Spotify 1.3.3.264 displayed To Disk alongside
ファイル, 編集 and 表示; its Downloads toggle changed the INI correctly.
These tests do not establish live support for every Spotify version or locale.

The earlier RC6 launcher was also run on installed Spotify 1.3.3.264 with its
version.dll. It exited successfully, the main process loaded the adjacent DLL,
and Spotify remained responsive. The Japanese To Disk menu, Downloads toggle
and four current-track rows worked after that launch. A second launch after
restoring the original language and INI also succeeded. Spotify 1.3.1 from
the startup report was not available for a live test; the System32-first
failure was reproduced with the Windows fixture instead.

## Automatic and Launcher modes

The release has exactly two assets: the Automatic-mode version.dll and a
Launcher ZIP containing Floggfy.exe and the same DLL bytes named Floggfy.dll.
The ZIP includes binary checksums and the default INI; the standalone DLL
creates the INI when missing. Mixed-mode installation is rejected before
shutdown or injection.

The Windows fixture confirms that normal Spotify startup does not load
Floggfy.dll, while launcher startup explicitly loads it before application
entry. Restart tests stop multiple matching processes and preserve an unrelated
installation whose DACL permits querying but denies termination. Initialization
failure cleanup and System32-first loading regressions still pass. The native
and full Windows suites passed after these changes.

Live Windows Spotify 1.3.3.264 checks confirmed normal startup in Launcher mode
loaded neither local DLL, then Floggfy.exe stopped every previously running
Spotify process from that installation and started a responsive new instance
with Floggfy.dll loaded. The To Disk menu and four current-track rows worked.
Launching plain Spotify again left Floggfy unloaded; a second launcher restart
and a restart from an already hooked session also succeeded. Automatic mode
was then restored and loaded version.dll normally with the menu working.
The original personal INI was restored exactly. Packaged binaries match these
live-tested files; media names and private diagnostics are excluded.

## Episode footer identity

The heartbeat now accepts current episode titles without requiring music album
or artist tags. Episodes use their URI identity and do not enter music enrichment
or borrow decoder details from the music identity. Regression tests cover missing
music tags, same-title episodes with different URIs, and unknown actual quality
with a lossless target preference. The title remains available without inventing
a quality level. Existing Ogg/FLAC association and complete-listen tests pass.

On installed Spotify 1.3.3.264, an episode previously cleared the whole footer;
the corrected build displayed its current title. Its cached actual quality and
format were unknown, so codec, sample rate and quality remained unavailable.
This is an episode-title correction, not evidence of a new live Ogg quality test.
The native and full Windows suites passed; the additional same-title identity
regression failed before its fix and passed afterward on Windows. Private
diagnostics were removed and the original personal INI restored for final use.


## Playback pause diagnostics (post-1.2.4)

The reported playback pause has not been reproduced or assigned a cause. The
reported FLAC frame-gap line comes from the capture coverage guard, after the
native frame callback returned success; it does not establish a Spotify crash.

The focused `bash test-playback-diagnostics.sh` gate covers unchanged paused
observations, immediate resumed status, raw playback transitions, exact FLAC
coverage reasons without altered acceptance, native callback return/data
preservation, bounded timing summaries, last-observed NLM context, and guarded
client diagnostics with identifier privacy. It passed in one execution: the
Windows executable and Node VM test both completed successfully. No test was
rerun and no broad Linux/Windows regression suite was executed. Existing vendor
warnings were not suppressed. Independent static review found no blocker.
The subsequent `bash build-native.sh` production DLL/launcher build passed.

The Windows media-service provider and NLM interception require live validation;
synthetic callbacks cannot establish the cause of issue #12 or prove the absence
of hook interference. At that initial gate the new DLL had not been installed;
the subsequent release-candidate installation is documented below.


## Default-off network compatibility patch

`NetworkPatch=0` disables the legacy NLM patch at process startup, including when
an older INI omits the key. Opting in with `1` requires a full Spotify restart.
The disabled path returns before import inspection, IAT writes or initialization
retry state; normal audio, menu and metadata startup remain separate. The
existing atomic settings writer adds missing keys and preserves explicit choices,
Unicode and unrelated sections. No live hook-detachment path was added.

The focused `bash test-network-patch.sh` gate passed in one execution. It verified
default/missing/explicit INI values, startup migration and Unicode/unknown-section
preservation, disabled invalid-pointer/import bypass, normal/delay import opt-in
and active delay-slot repair. It uses synthetic import tables and an isolated
Unicode INI; it does not launch Spotify or contact a network service. No previous
test gate or full suite was rerun for this follow-up. The subsequent
`bash build-native.sh` production DLL/launcher build passed with existing vendor
warnings visible. The current Windows release's compatibility with the enabled
patch and the cause of issue #12 remain unverified. Subsequent startup
installation evidence is documented below.


## 1.2.5-RC1 prerelease

The candidate's native code matches the test build from `63d5eb6`. A production
DLL/launcher build after the final INI explanation change passed. On installed
Spotify 1.3.6.1921, Launcher mode loaded the exact candidate DLL, exposed its
startup-ready signal and a visible window. With `NetworkPatch=1`, `Log=1` and
`DebugLog=1`, startup logs confirmed the delay-import patch and NetworkListManager
hook installation. Other saved preferences were preserved.

This is installation/startup evidence. Sustained playback reliability, the cause
of the reported pauses and compatibility across Windows versions remain
unverified. The published sample INI keeps `NetworkPatch=0`; opt-in requires a
full Spotify restart. The two earlier targeted gates each passed once, and no
test gate or broad regression suite was repeated for prerelease packaging.

Prerelease artifact checks verified that the standalone DLL and Launcher ZIP's
DLL exactly match the installed native build. The ZIP's 12-entry allowlist,
CRC checks, binary hashes, default INI, six vendor licenses and both checksum
manifests were verified. The package includes no personal INI, music, logs or
private deployment scripts. These packaging checks do not extend playback or
codec validation coverage.


## Connect diagnostic context after RC1

Static inspection of Spotify 1.3.6.1921 identified the cached
`ConnectDevicesAPI.getConnectState()` shape and the PlayerAPI `action` event.
Connect state describes the playback target; PlayerAPI's mapped snapshot omits
an incoming command's sender. Logs therefore keep controller attribution unknown
and only mark directly observed local client requests as local. They do not infer
remote control from a device list or a missing local action.

`DebugConnect=1` is gated by debug logging and the existing metadata collector.
The focused `bash test-connect-diagnostics.sh` gate passed in one execution:
the Windows INI fixture covered defaults, explicit opt-out, missing-key migration,
Unicode/unknown-section preservation and capture-independent persistence. The
Node VM fixture covered cached local/remote/unknown targets, honest unknown
initiators, passive local requests, paused deduplication, bounded observations,
opt-out and cleanup failure, accessor privacy and no additional network/control
calls. No earlier gate or broad suite was rerun. Independent static review found
no blocker. The subsequent `bash build-native.sh` DLL/launcher build passed;
existing vendor warnings remained visible. The implementation has not been
installed for live Connect validation.
RC1's existing assets/tag and the running installation remain unchanged.


## Decoded Connect request observer after RC1

Static analysis located a common complete-JSON boundary for plain and compressed
Connect requests. The compressed path finalizes its inflater before calling this
boundary; observing only the feed return can miss trailing plaintext. Capability
discovery checks the command-parser owner, unique string references, shared
const-string adapter, input layout and the two complete-message returns. It
excludes the compressed envelope and uses no fixed production RVAs.

`DebugConnectMessages=0` keeps this optional native detour off. The requested
implementation is read-only: original arguments, input storage, return value,
last error, dispatch and acknowledgements remain with Spotify. A bounded summary
records actions, message IDs and anonymous sender labels; receipt and execution
are separate. No message rewrite mode is present.

The single focused `bash test-decoded-connect.sh SPOTIFY_DLL` gate passed in one
execution. It combined synthetic capability rejection cases, a static mapped
copy of the installed Spotify 1.3.6.1921 DLL, bounded summary parsing, fake original
callback forwarding, AL/last-error preservation, queue contention/overflow and
redaction, raw-slot clearing, and isolated Unicode INI persistence. Independent
static review found one exception-specification mismatch in test stubs; it was
corrected before execution, then the review approved the gate. No test rerun or
broad regression suite was performed. The production DLL/launcher build passed.
A subsequent statically reviewed idle-path optimization avoids allocating and
clearing a 16 KiB record when no observation is pending. The targeted runtime
gate was not rerun after this final correction; its runtime behavior remains
unverified. A final production DLL/launcher compilation after that correction
passed with existing vendor warnings visible. The older network-option fixture has link stubs for the new startup dependency and is not
rerun. Neither source-level review nor a synthetic callback establishes live
hook safety, sender authenticity, successful execution or cross-version support.
RC1's assets/tag and the running installation are unchanged.


## Separate deep networking diagnostics (post-RC1)

`DebugNetworking=0` is the default-off, restart-only master switch for the native
decoded Connect hook and `Floggfy-networking.log`. Enabling it also requires
`Log=1` and `DebugLog=1`. It supersedes the unshipped `DebugConnectMessages`
option, which remains inert if retained as an unknown key in an old INI.
The native observer/caller filters and unchanged original command forwarding
remain. Known client state/local action summaries, native request summaries,
our enrichment request/results and numeric capture/timing context go into a
separate bounded queue on the existing logger worker. Neither raw messages,
media, credentials, URLs nor real device identifiers are written. Renderer
input must match exact field/value allowlists. UTC milliseconds, monotonic
time, process/sequence numbers, operation IDs and anonymous sender labels
support correlation; they do not establish command execution or pause causality.
Each log retains at most 5 MiB by removing oldest whole lines. Queue losses
are counted. Unsupported/unobserved paths stay unknown.

`bash test-networking-log.sh SPOTIFY_DLL` **passed in one combined execution**:
actual Windows file writing and whole-line eviction, default/missing/explicit
settings, inert old-key behavior, Unicode/unknown-section persistence, master
opt-out, logger queue isolation/contention/overflow accounting, timestamps,
strict field rejection, bounded JSON/capability checks against a statically
mapped Spotify 1.3.6.1921 DLL, fake callback argument/AL/LastError preservation,
and Node enrichment request/result/status/latency, privacy, cached Connect,
local-action cleanup and unchanged-paused-state cases. No Spotify DLL was
executed or injected by this gate; no network request or playback command was
sent. The earlier idle-drain optimization is now exercised by this combined
gate's queue-draining fixture. No full suite or test rerun was performed.

`bash build-native.sh` **passed** afterward for the production DLL and launcher.
Existing vendor warnings remained visible. Independent static review found two
fixture problems (shared INI directory and missing `wmain` return), corrected
before the single test execution, and no production blocker. The only subsequent
changes were documentation.

The prior decoded-observer revision was installed on Spotify 1.3.6.1921 and
reported observer activation and a responsive window. This new separate-log
revision has **not been installed or live-tested**. Actual remote request
receipts, artwork HTTP failure timing, prolonged playback, other Spotify/Windows
versions and pause causality remain unverified. Earlier sections above describe
historical builds and their limitations. RC1's tag/assets and the running
installation are unchanged by this revision; no merge or release was performed.


## Quiet hook timing summaries (post separate-log deployment)

Routine nonempty counters previously generated one line per metric every ten
seconds in both diagnostic logs. Routine intervals now drain counters silently
when all maxima are below 10 ms and there are no zero-byte reads. Pauses,
timeline jumps, native/capture/media-observation errors, zero-byte reads and
maxima at/above 10 ms emit one compact summary. The cutoff is a reporting policy,
not an audio acceptance rule or proof of a pause's cause. Measurements remain
approximate intervals since the last drain; no additional hook or INI option
was introduced and the original callbacks/capture predicates are unchanged.

`bash test-playback-diagnostics.sh` **passed in one targeted execution** after
implementation/static review: Windows original-callback semantics and existing
pause/coverage checks plus deterministic short-timing suppression, one-line
pause/error summaries, a threshold-crossing queue wait, empty-read reporting,
counter drain behavior and debug opt-out. The Node client-observation fixture
also passed. No full suite or test rerun ran; existing vendor warnings were
visible. `bash build-native.sh` then passed for the production DLL/launcher.
The quieter build was not installed, preserving the active playback test;
no live timing-reduction result is claimed by this gate.


## Opt-in activity diagnostics and launcher architecture preflight

`DebugHookDiagnostics=0` defaults off. With `DebugNetworking=1`, `Log=1` and
`DebugLog=1`, setting it to `1` before a full restart enables bounded numeric
menu/startup activity scopes and capture-worker progress sampling. Producers
use 32 fixed slots and lock-free integer atomics, never allocating or writing
logs in these scopes. Existing hooks/CEF callbacks still run normally; no new
hook, thread, exception handler, thread suspension or memory dump is introduced.
The existing logger worker reports completed spans >=10 ms, sampled spans still
active >=250 ms, capture progress gaps >=1 s and recovery. Pending/stall reports
repeat at most every 5 s and diagnostic-slot omissions are counted. These are
wall-clock observations, not CPU profiling or proof of a pause cause. Logger,
storage or whole-process stalls can delay/prevent reports. UI-thread and live
interleaving behavior have not been validated on Spotify for this revision.

The launcher now inspects both adjacent executable/DLL headers before stopping
existing Spotify processes. Unsupported x86/ARM64 architectures and bounded
header/read failures get specific errors. It requires complete architecture-
specific optional-header reads and a present declared header/section-table
extent; this is an architecture/header preflight, not full Windows loader or
Spotify ABI compatibility validation. Automatic DLL mode also requires x64
Spotify, but an incompatible DLL cannot run its own error-reporting code.

`bash test-deep-diagnostics.sh` **passed once** after static review: deterministic
short/slow/pending scope timing, repeat suppression, completed-slot reuse,
stale-token rejection, bounded overflow, backwards-finish handling, progress-gap
recovery, all diagnostic prerequisites/opt-out, scope `LastError` preservation,
Unicode/missing-key settings persistence, and synthetic x86/ARM64/truncated/
undersized PE rejection. Mixed-architecture `Launch()` cases fail before process
control; no Spotify process or network operation was executed. Review identified
an undersized optional-header acceptance path and incomplete fixtures; both were
corrected before that single test execution. No test rerun/full suite occurred.
Existing playback/capture/CEF suites were not rerun for this change.

Both previously installed and prior built DLL imports were inspected: Windows
system libraries, including `msvcrt.dll`, with no separate VCRUNTIME/MSVCP/GCC
runtime DLL import. Build flags statically link libgcc/libstdc++ and do not request
host-native/Intel-specific instruction extensions. This does not establish
cross-machine compatibility or explain the issue reporter's startup failure.
The targeted gate passed before one final static heartbeat-placement correction:
worker progress now also advances after each processed decoder event, preventing
an uninterrupted queue drain from looking stalled. That placement correction
was not retested, respecting the one-execution limit. The production DLL/launcher
build passed before and after it; final imports still contain no separate VC/GCC
runtime DLL. Existing vendor warnings were left visible. The new activity timer
uses Windows tick-count wall time with its native clock resolution; thresholds
are approximate reporting cutoffs, not microsecond profiling or acceptance rules.
This revision has not been installed; existing local test playback is preserved.


## PR #13 review corrections: startup INI and diagnostic gating

Ordinary menu persistence inserts missing startup defaults but preserves existing
`NetworkPatch`, `DebugConnect`, `DebugNetworking` and `DebugHookDiagnostics`
entries from the INI text read for that save. It does not copy the old in-memory
startup values over those entries. The startup reader shares the INI section/key
syntax, reads the bounded file once for these four flags, and requires an exact
unquoted `1` after trimming spaces/tabs. Blank/malformed/duplicate entries fail
closed; missing keys retain documented defaults. This does not provide a
transaction against an editor modifying the file concurrently with replacement.
Unknown sections, Unicode and the existing staged settings writer are retained.

One effective `Log && DebugLog` condition now gates native callback/queue timers,
diagnostic enqueueing, networking activity and the injected renderer options.
The renderer option builder is shared with its targeted fixture. The shipped and
generated INI group the dependent debug switches directly below logging and
explain their prerequisites. README remains a short getting-started guide.

Connect discovery is organized into named command-owner, input, parser-call and
inflater-pipeline checks, with named layout constants/instruction comments.
Byte patterns, rejection statuses, scan bounds, caller counts/order, validated
adapter ABI and output return addresses are retained. No new Spotify layout or
version capability is claimed.

The focused review gate is `bash test-pr13-review.sh SPOTIFY_DLL`: isolated
external-edit persistence/migration/Unicode/strict parsing and renderer-option
fixtures, actual callback/queue timing opt-out, the existing native forwarding
checks, bounded synthetic/mapped-client discovery, and the JavaScript debug
fixture. It sends no playback command or network request and never executes the
mapped Spotify DLL. Its execution result and tested commit are recorded in
PR #13. No full regression suite is implied. Off/on live playback remains a
separate required user validation before merge; the pause cause is unconfirmed.


## Renderer diagnostic privacy follow-up

The renderer status gateway validates before selecting a log or invoking the
ordinary native failure classifier. Every reserved diagnostic prefix is claimed,
including invalid/disabled/oversized input; it cannot fall back to raw console
logging. Known status fields are sanitized both with and without deep networking.
Two exact metadata lifecycle literals remain accepted; unknown status text is
rejected. Embedded NULs and non-ASCII statuses are not truncated or converted
lossily, and the temporary ASCII copy is cleared. Generic diagnostic/activity/
networking APIs defensively route reserved prefixes through the same validator.
This correction adds no hook, thread, network request or payload logging.

The focused regression command is `bash test-renderer-privacy.sh`. It uses the
actual renderer gateway, HistoryLog, every logger queue entry point, and actual
file writing in an isolated directory. Cases cover all Log/DebugLog/networking
mode combinations; malformed connect/control/unknown statuses containing native
failure keywords; oversized, Unicode, NUL and numeric/enum rejection; positive
canonical routing; two fixed startup messages; and normal native failure logging
without DebugLog. Exact commit and execution results are recorded in PR #13.
No Spotify/CEF process or network operation is used by this fixture, and no full
regression suite or live renderer integration is implied by that result.
