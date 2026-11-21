# Native S–Z audit

Locations refer to the v1.4 source. Recommendations are based on static inspection.

## Candidates worth evaluating first

### 1. Avoid constructing unchanged song destination paths on every observer update

**Functions:** `PublishSongDestination` (`song_destination.cpp:300–331`), `IntentKey` (98), `SameRequest` (54), `DestinationWorker` (247).

The publisher computes its intent, then copies the catalog and constructs all four normal output paths and up to sixteen duplicate paths before obtaining the try-lock or testing `SameRequest`. An unchanged observer heartbeat therefore repeats path sanitization/allocation even while an identical probe is already in progress or its answer is cached. The worker's verification cache saves disk work but not this publisher-side work.

**Fix:** retain the original path inputs with the latest request and perform a cheap equality check under the existing try-lock before constructing paths; reuse/coalesce the unchanged request and preserve the existing heartbeat refresh. Do not trust the 64-bit intent hash alone as proof of equality. Preserve serial, revision, settings-generation and cancellation checks. If path construction remains necessary, perform it outside the lock as now. This is a credible steady-state optimization with no new persistent cache.

### 2. Reduce stream-dump copying while holding the producer lock

**Functions:** `stream_dump::Writer` (`stream_dump.h:216`, full-record copy at 261), `Input` (138), `Playback` (172), `CaptureEvent` (154), `Decision` (124).

Each `Record` contains a 65,536-byte array. `Writer` assigns the entire record under the global SRW lock even when `length` is only 16–48 bytes for identity or a small metadata message. Native decoder input calls `stream_dump::Input` directly (`audio_history.cpp:711`) and takes that same exclusive lock. Dumping is opt-in, but this is avoidable lock occupancy in the diagnostic path we use to investigate capture.

**Fix:** copy the fixed record header and only `length` payload bytes while dequeuing, preserving ring ownership and sequence order. Do not release the slot and then read its bytes unlocked. Preserve separate decision/raw/capture gap accounting and explicit incomplete markers. A pointer-swap ring would add complexity and is not necessary for this improvement.

### 3. Stop rediscovering the entire COM import table after the connectivity hook is active

**Functions:** `StartConnectivityHook` (`version_proxy.cpp:324–350`), `HookSpotifyImports` (288), `StartupMonitor` (403; monitor body 457–489).

With the optional NetworkPatch enabled, the active path calls `HookSpotifyImports` each monitor iteration. That re-parses the PE and searches normal/delay imports by API name. Monitor cadence is 25 ms for the initial 600 iterations, then one second. The legitimate requirement is to detect a delay loader overwriting an already discovered slot, not repeatedly rediscover all imports.

**Fix:** retain validated normal/delay slot addresses for the specific loaded module and image bounds, inspect those slots on the normal monitor cadence, and re-patch only changed slots. Re-discover on a module identity/layout change or invalidation. Keep shared monitor retry behavior and optional-off behavior. Do not remove delay-import verification.

### 4. Reuse discovery results within one audio hook startup attempt

**Functions:** `DiscoverDecoderOwnershipLayout` (`spotify_hook_discovery.cpp:402–416`), `DiscoverHookTargets` (285), `DiscoverDecodedOutputTarget` (352), callers in `audio_history.cpp:4158–4162`.

Ownership discovery independently invokes the base capture discovery and decoded-output discovery, although the caller already resolves decoded output and base capture targets in the same startup attempt. These functions scan executable sections and validate runtime-function boundaries. This is startup-only work, not a steady-state audio hotspot.

**Fix:** expose an internal helper accepting the same validated `Image`, `HookTargets`, and `DecodedOutputTarget`; keep the existing independent public entry points for tests/callers. Reuse only within an attempt while the same module is held. Preserve every uniqueness, call-chain, vtable, boundary and legacy/current ABI validation. Do not turn this into a global version-based address cache.

### 5. Avoid decoding dump payloads after the dump log has stopped accepting data

**Functions:** `DrainDumps` (`telemetry_filter.cpp:333–355`), `AppendDump` (319), `FormatDump` (273), `Capture` (175), `ObserveRendererDump` (582).

The worker calls `AppendDump(FormatDump(*record), full)`, so decompression, protobuf parsing, JSON formatting, and allocations precede discovery that the capped log cannot accept the text. A failed append due to the 5 MiB cap also leads to further summary append attempts whenever counters change. Payload capture has a 32-request cap, so scope is bounded, but repeated drop-count summaries can still reopen a full file.

**Fix:** track a known-full sink state and avoid payload decoding/writes while it remains full, retaining counters and a single warning. Decide explicitly whether deleting/truncating the log during the session resumes dumping; if retained, use a low-frequency size/revision check rather than decoding first. Preserve diagnostic counts and do not suppress normal privacy blocking. This affects only explicitly enabled dumping.

## Smaller, lower-priority opportunities

- **`settings_file::Set` (settings_file.h:122) and `Erase` (159):** allocate copied/trimmed strings for every line, unlike view-based `Find` (98). Use `wstring_view`, `TrimView`, and `EqualKey` for comparisons, discarding/reconstructing views after mutations. Typical INIs are small and writes are infrequent: low priority. Preserve comments, unknown sections, duplicate handling, Unicode validation and atomic staged replacement in `Write` (55).
- **`InflateGzip` (telemetry_decode.cpp:30–56):** allocates/zero-fills the full 1 MiB output ceiling even though gzip ISIZE has already been checked against the ceiling. A buffer bounded by validated ISIZE can avoid excess initialization; handle zero output and reject any actual expansion beyond the claimed size. Only optimize alongside malformed gzip tests; do not weaken checksum or expansion limits. Diagnostics-only, low priority.
- **`ValidateVorbisHeaders` (vorbis_headers.cpp:44–76):** allocates an 8 MiB arena each validation, then serializes stb header opening on `parser_lock` because stb initializes shared CRC state. Consider worker-owned scratch reuse only if existing-file scans show material repeated cost. Do not remove the parser or mutex without proving upstream initialization is safe; it validates semantic setup packets that structural Ogg page checks cannot establish.
- **`ResolveVersionExport` (version_proxy.cpp:53–67), ASM `FORWARD` (version_exports.S:4):** every forwarded version call resolves its export again. A thread-safe per-export resolved address table could avoid repeated `GetProcAddress`, but version APIs are not an audio hot path. Preserve lazy System32 loading and exact assembly register/stack forwarding, especially undocumented `GetFileVersionInfoByHandle`.
- **`Populate` (to_disk_menu.cpp:182–276):** it now returns immediately if the root command exists, yet still retains the older find-existing-child/`inserted` refresh structure and conditional `IndexOf`/`SetLabel` logic for items in a newly constructed child. Simplify to an explicitly construction-only helper after validating expected CEF lookup semantics. Keep runtime updates in `WillShow`/`Format` and command handling. This is removal of obsolete structure, not a reason to remove capability validation.
- **`Execute` (to_disk_menu.cpp:463–561), `SettingName` (88):** ordinary boolean settings are repeated across a switch, long value-selection ternary, label-selection ternary, and logging names. Existing `fallback_items` already demonstrates a small command table. Extend that pattern to ordinary setting commands, keeping network/update changes and file pickers as explicit special cases. Mostly maintenance, not runtime gain.
- **`ReadSpac`/`SpacStream::ParsePage` (spac_stream.cpp:196/120):** publication extraction re-parses page layout already measured in the caller; `Push` also calls `Layout` then `ParsePage` calls it again. A private `ParseMeasuredPage` can reuse layout once the same bytes are complete. Preserve checksum/config/frame-continuity checks. Capture does not allocate one vector per audio packet; that happens only during final extraction, so do not claim a capture-time packet allocation problem.

## Useful source-file boundaries (no behavior changes required)

- `version_proxy.cpp` combines version export forwarding (40–71), optional connectivity COM/IAT repair (73–350), and startup/service orchestration (352–502). Split connectivity repair into its own unit; leave DllMain minimal and keep startup ordering explicit.
- `telemetry_filter.cpp` combines CEF callback interception, bounded payload capture, dump rendering/file writes, renderer message decoding, and coverage reporting. Extract the dump worker/formatting module first. This makes the request callback's behavior easier to audit without changing ABI/refcount ownership.
- `to_disk_menu.cpp` combines CEF ABI adapter/lifetime, menu model construction, state refresh, setting commands, and Explorer actions. Extract setting command descriptors and Explorer click actions. Do not separate lifetime callbacks in a way that obscures transferred references.
- `stream_dump.h` is a 379-line implementation header with global inline state, raw callback entry points and a large writer state machine. Move the writer/lifecycle and JSON/file helpers to `.cpp`; keep a small declaration interface. Separate raw-stream versus worker-capture serialization helpers inside the implementation, preserving a single sequence-ordered queue drain.
- `spotify_hook_discovery.cpp` has a natural split between PE/pattern helpers + audio ownership discovery (1–502) and Connect command/pipeline discovery (504–784). Share a private image view rather than duplicate parser code. The two ABI validators are intentionally distinct evidence, not redundant code to collapse.

## Keep / no proven removal, by remaining file or function family

| File/family | Assessment |
|---|---|
| `song_destination.cpp`: `CurrentRequest`, `VerifySavedFile`, `ProbeSavedSong`, `RefreshSongDestination`, `ReadSongDestination` | Keep stale-result prevention, signature-before/after checks, bounded positive/negative verification cache and worker-only I/O. Four-format/duplicate resolution is feature-required; do not remove collision checks merely because normal names were already tried. |
| `spotify_hook_discovery.cpp`: `Read`, `Range`, `ParseImage`, `ExecutableRange`, `RuntimeFunction`, `FindTarget`, `Consistent`, `DirectBranch`, `PointerRva`, `UniqueVtableSlot`, `UniqueContextThunk`, all `Connect*`/`VerifyConnect*` functions | Keep semantic/uniqueness validation. `FindTarget` already uses memchr before memcmp. No safe dead-function removal established. Splitting and within-attempt result reuse are the justified changes. |
| `spac_stream.cpp`: layout/checksum/bit/config helpers and extraction budget | Keep bounded codec validation and resource accounting. Limits/configuration/continuity have separate purposes. No simplification by guessing duration or relying solely on GUI state. |
| `startup_launcher.cpp`, `startup_launcher.h`, `startup_ready.h` | Keep exact installation/process/path checks, architecture validation, loader-owner rebasing, entry-stop, ready handshake, timeout failure cleanup. Startup-only bounded polling is appropriate; replacing it with sleeps or broad process termination would regress behavior. No optimization need proven. |
| `update_blocker.cpp/.h` | Keep handle-based directory checks, marker-specific ACE handling, effective-access verification and rollback. `Change` (106–240) could be factored into enable/restore/verify helpers for reviewability, but each branch protects user ACLs. This is user-click/startup work, not a playback hotspot. |
| `telemetry_policy.h` | Keep allocation-free bounded URL parsing and explicit host/path boundaries. Fixed-array scans are tiny; no regex/map replacement needed. |
| `telemetry_schema.h` | Generated factual schema table. Keep as generated data; do not hand-prune seemingly unused aliases without regeneration/source evidence. |
| `telemetry_decode.cpp/.h` | Keep bounded generic protobuf fallback, schema recognition, unknown-field preservation, UTF-8/gzip integrity checks and depth/node/output caps. A small linear schema scan is adequate; no new cache/index needed. |
| `telemetry_inflate.cpp` | Four-line wrapper compiles the vendor inflater as C++/extern C as required by the build. Keep; it is not duplicated application logic. |
| `telemetry_crash.cpp/.h`, `telemetry_crash_command.h` | Keep exact executable matching, argument-boundary parsing, raw command preservation, preserving GetLastError, and trampoline lifetime. Parsing is performed only for relevant process launches; no broad command-line rewrite. |
| `telemetry_queue.cpp/.h` | Keep exact known queue filename, no-follow/reparse/path validation, directory handles, and deletion of the validated handle. Removing those checks would widen destructive filesystem behavior. Calls occur at startup/exit, so per-path temporary buffers are not a meaningful hot path. |
| `telemetry_queue_exit.cpp/.h` | Keep once-only exit reentry guard and intentionally retained callback/trampoline state. Exit logging intentionally avoids an async queue that cannot drain. Do not unify with normal logger teardown or free callback storage at detach. |
| `telemetry_filter.h`, `to_disk_menu.h`, `song_destination.h`, `spotify_hook_discovery.h`, `vorbis_headers.h` | Small declarations/contracts; keep. No function-level removal proven. |
| `srw_guard.h` | Tiny RAII lock ownership helpers; keep. They do not make an inappropriate blocking caller safe, but replacing them with manual lock/unlock worsens exception safety. |
| `tag_memory_plan.h`: `PlanTagConstructionMemory`, `LargestOggPacket` | Keep overflow-safe publication memory planning and packet bounds. Conservative multipliers can only be tightened with measured peak allocation evidence; not obvious bloat. |
| `version_exports.S` | Keep full export list and Windows x64 ABI tail forwarding. Any export-address caching belongs in resolver initialization, not a wholesale assembly rewrite. |

## Evidence limits

These are source-review findings, not benchmark results or a claim that any item caused Spotify to pause. I found no confidently dead public function in this file slice. The best removal opportunities are obsolete menu construction branches and duplicate discovery/path-construction work, not defensive validation or rollback paths.
