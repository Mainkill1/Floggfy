# Native A–H function audit

Locations refer to v1.4. This review covers capture, codec, artwork, settings and publication functions. The CSV inventory includes their inline callbacks.

## Priority remedies

### 1. Release auditing still serializes capture behind whole-recording validation

**Functions:** `audio_history.cpp:1162 CheckBeforeRelease`, `:1508 ReserveConstruction`, `:1807 QueueReleaseAudit`, `:1822 DrainOneReleaseAudit`, `:2366 CaptureBudgetScope`, `:3327 ProcessCaptureEvent`.

**Evidence:** `CheckBeforeRelease` keeps `MemoryBudgetLock budget` alive across cached validation, `Flatten()`, and the independent whole-source parser. SaveWorker calls it through `DrainOneReleaseAudit`; capture acquires the same exclusive SRW lock for every event. Moving the audit to the save thread therefore does not eliminate its capture stall. This is a code-proven lock dependency; actual stall duration needs workload measurement.

**Remedy:** give release audits an explicit, charged construction lease. Reserve under a short critical section, release the lock before copying/parsing, and reconcile/release the lease afterward. Keep independent media validation and truthful unavailable outcomes. Test by pausing the audit parser after reservation while admitting a capture event; assert progress and exact total reservations rather than imposing a flaky time threshold.

### 2. Release-audit jobs double-count their own retained memory

**Functions:** `audio_history.cpp:1807 QueueReleaseAudit`, `:1822 DrainOneReleaseAudit`, `:1508 ReserveConstruction`; compare `:3758 QueueRecording`.

**Evidence:** QueueReleaseAudit adds `job.reserved` to `publishing_reserved`, but never sets the moved capture's `publication_reserved`. ReserveConstruction calculates `other = publishing_reserved - capture.publication_reserved`, then adds `capture.Capacity()` again. A normal active capture has zero `publication_reserved`, so its retained bytes are counted twice. QueueRecording explicitly sets that field. Near the limit this can turn a feasible final check into “validation unavailable.”

**Remedy:** use one reservation owner shared by SaveJob and ReleaseAuditJob. Do not merely set the field: ReserveConstruction may grow/shrink it, while DrainOneReleaseAudit currently subtracts the original `job.reserved`; cleanup must release the final lease. Add a boundary accounting test covering audit admission, temporary peak, queue rejection, parser failure, and final release.

### 3. Every event dequeues a 65 KB slot while holding the producer lock

**Functions:** `audio_history.cpp:502 Pop`, `:511 FlacEvent`, `:478 Hook`; `Slot` at `:108`.

**Evidence:** `s=queue[head]` copies the entire slot, including `bytes[65307]`, under QueueGuard. A successful FLAC frame callback queues only 32 payload bytes; lifecycle events queue none. Producers use that same lock. This is guaranteed excess copied data, without needing to infer the compiler's exact machine instruction sequence.

**Remedy:** copy the scalar event header and exactly `length` payload bytes. Prefer an explicit header structure to error-prone field-by-field duplication. Keep producer sequencing, kind/epoch/generation fields, and lock lifetime unchanged initially. Test mixed reused slots and maximum payloads; measure bytes copied/critical-section work in addition to timing.

### 4. Capacity accounting repeatedly traverses every retained capture

**Functions:** `audio_history.cpp:999 Capture::Capacity`, `:2367 CaptureBudgetScope::~CaptureBudgetScope`, `:2961 ScanFlacCapture`, `:2981 ProcessFlacCapture`, `:3081 ProcessOggCapture`, `:3174 ProcessAacCapture`, `:3233 ReconcileMp3Bytes`, `:3276 ProcessMp3Capture`.

**Evidence:** a normal FLAC byte event totals all active/Ogg/ready captures before Append, totals them again before Scan, and totals them again in the scope destructor. AAC invokes its summing lambda before and after Push plus the destructor. Capacity itself walks unmatched attempts and string capacities. Work grows with events × retained captures even when most allocations do not change.

**Remedy:** first centralize the repeated summation; then track per-capture charged capacity and update deltas on actual allocation/ownership transitions. Keep allocator-capacity accounting, temporary construction peaks, and a debug recomputation assertion. Do not blindly increase the fixed queue to compensate.

### 5. Already-queued filename checks still construct enriched tags and copy cover art

**Function:** `audio_history.cpp:1849 QueueFilenameCheck` (dedup check `:1895`).

**Evidence:** CatalogFor, tag-string construction, EnrichTags and ApplyCatalogIdentity precede the check that this serial was already queued. The intent key depends on catalog/settings/quality and MetadataRevision, not the newly built Tags payload. Worker calls this for fresh ready observations.

**Remedy:** construct/compare the cheap intent key first; build enriched Tags only for an unsubmitted intent. Keep the current rule that a new intent invalidates the previous serial even when the producer loses the try-lock. Verify a repeated identical observation does no artwork fetch/copy and a metadata revision still queues exactly one refresh.

### 6. Artifact keys duplicate the entire artwork payload

**Function:** `audio_history.cpp:1420 PublicationArtifactKey`, caller `:1545 Publish`.

**Evidence:** `text(std::string(tags.cover.begin(),tags.cover.end()))` creates a temporary cover-sized string and appends it to the retained ArtifactKey; Tags already owns those bytes. Artwork can be 4 MiB. Key construction occurs outside its later construction reservations.

**Remedy:** use an exact structured artifact identity: scalar/tag fields plus equality against immutable retained artwork, or an immutable artwork snapshot with a content revision. A digest can prefilter equality; do not make collision-prone hashing the only proof of identical publication bytes. Include retained key/snapshot capacity in accounting.

### 7. Matcher diagnostics pay the computation cost before their suppression gates

**Functions:** `audio_history.cpp:3369 LogMatcherSnapshot`, `:3387 EmitMatcherSnapshot`, `:3437 AssociateRetainedRecordings`, `:3793 ScheduleStreamPublications`.

**Evidence:** every scheduling pass rebuilds bounded competitor/candidate snapshots for each retained capture and walks metadata history. EmitMatcherSnapshot only compares the signature afterward. LogMatcherSnapshot serializes records before `matcher_diagnostic_budget.Admit(...)`; a rejected detailed snapshot still incurs serialization/allocation. Candidate and native-match processing also repeat on the worker's 50 ms idle cadence, including nested lifetime searches in `BindDirectReadyCapture:2578`.

**Remedy:** separate semantic matching from diagnostic materialization. Track changes to source coverage, metadata generation/content, ownership, settings and retention counts; reuse an unchanged diagnostic snapshot. Add an explicit diagnostic budget/remaining-byte preflight to avoid building records that cannot be admitted while preserving omitted counters and terminal disposition evidence. Do not cache matcher decisions solely by metadata generation: duration enrichment, completion and competing captures can change an earlier result. Benchmark/operation-count debug-on and debug-off separately.

### 8. Cache-key filtering allocates repeatedly for a constant query

**Functions:** `cached_metadata.cpp:160 Wanted`, `:168 Record`, `:386 CollectMetadataTable`, `:433 CollectMetadataLog`.

**Evidence:** Wanted constructs `"#$" + uri + "#"` for each cache-prefixed key, including nonmatches. Record materializes `std::string(key)` for count then again for indexing. A temporary harness compiled against the current production `.cpp` measured **10,000 rejected Wanted calls → 20,000 heap allocations** (`c++ -std=c++17 -O2 -ffunction-sections -fdata-sections ... -Wl,--gc-sections`). The counter wrapped global operator new, was reset after constructing the input strings, and counted calls while matching an unrelated cache-prefixed key against a track URI.

**Remedy:** precompute the query suffix once per table/log collection and pass a string_view predicate. Use one lookup/emplace for each matching key. Preserve transactional outputs, sequence ordering, tombstones, CRC checks and record limits. Regression: operation/allocation count independent of rejected-key count after query preparation.

### 9. Known track/album queries parse and decompress the same cache file twice

**Functions:** `cached_metadata_windows.cpp:86 Collect`, `:141 ReadStoredMetadata`; the pure CollectMetadataTable/Log above.

**Evidence:** Collect reads each file once but invokes the entire table/WAL parser for the track URI and again for the known album URI. When album identity is learned from the first pass, ReadStoredMetadata can collect files again under the same read budget; that dependency is real, unlike the known-album duplication.

**Remedy:** let one validated traversal select both exact query suffixes into separate outputs. Keep unknown-album discovery as an explicit bounded second stage unless a bounded relevant-record cache can avoid it. Do not interpret an unreadable WAL as evidence that existing SST values are current. Tests should preserve invalid-second-record rollback and tombstone behavior.

### 10. Client transitions are turned into invented playback metadata and reparsed

**Functions:** `client_playback.h:30 ParseClientPlayback`, `:96 ParseClientTransition`.

**Evidence:** ParseClientTransition builds a map, synthesizes a second protocol containing `title=transition&artist=transition&album=transition&duration=1...`, and calls ParseClientPlayback, which builds another map and parses rich metadata to validate URI, instance and clock fields.

**Remedy:** extract shared URI/instance/finite-clock validators; parse transition fields once into their real destination. Fixed field IDs plus presence bits/string_views can later replace map allocation. Preserve duplicate/unknown-field rejection, URI alphabet, instance decoding limits and all natural/explicit/terminal timing gates; do not make transition records pretend to be metadata records.

### 11. Avoid writes/copies that do not contribute retained media

**Functions:** `compressed_buffer.cpp:6 Append`, `:58 Flatten`, `audio_history.cpp:1053 ValidateOggCapture`, `:1075 ValidateAacCapture`.

**Evidence:** make_unique<uint8_t[]>(65536) value-initializes every block before Append overwrites its used prefix. Validation reconstructs Ogg pages/AAC chunks byte by byte through operator[], repeatedly doing block indexing. Flatten is destructive by design and releases the blocks; treating it as a harmless read would be incorrect.

**Remedy:** allocate uninitialized blocks where the language/library supports it and retain the strict length invariant; add a block-aware read/copy-range API for validators using memcpy across spans. Do not flatten all captures just to simplify iteration. Keep memmove in compaction because source/destination overlap. Verify boundary-spanning ranges and uninitialized-tail exclusion with sanitizers.

### 12. Artwork copies hold the cache mutex

**Functions:** `album_artwork.cpp:18 AlbumArtworkCache::Cache`, `:51 AlbumArtworkCache::Get`.

**Evidence:** Get obtains a try-lock then copies the full cached image while locked; Cache compares image byte vectors while locked before swapping. Other optional lookups can fail the try-lock for the duration of a multi-MiB copy.

**Remedy:** immutable shared image snapshots: capture an owning pointer under lock, copy/use outside it; compare candidate content outside lock and recheck entry order before installing. Keep the eight-entry limit, pending-request ownership, retry backoff and revision-on-visible-content-change semantics. Pair with remedy 5 before adding broader machinery.

### 13. Destination candidate generation does quadratic string deduplication

**Functions:** `existing_quality.cpp:198 RecordingPathCandidates`, `:246 ResolveRecordingDestination`.

**Evidence:** up to several legacy base names × 32 collision suffixes × current/legacy identity; each insertion scans the growing vector with std::find. ResolveRecordingDestination separately constructs legacy-inclusive and writable candidate sets with substantial overlap.

**Remedy:** one deterministic candidate plan recording writable/legacy provenance; preserve order with a separate exact-string membership set if needed. Bound aliases and collisions as today. Keep “scan every possible existing home before selecting a vacancy,” quality/identity validation and explicit conflict behavior; shortcutting those creates duplicate or misowned songs.

### 14. Reuse the held-file snapshot within a publication transaction

**Functions:** `file_publication.cpp:185 PublishBytes`, `:82 PrepareBackupSlot`, `:31 ReadLockedRecording`; `existing_quality.cpp:361 DecideSave`.

**Evidence:** upgrade path inspects via DecideSave before acquiring old, repeats DecideSave after locking the existing object, reads the held object into `existing`, then PreserveUpgradeMetadata parses it again. Some repetitions are essential race checks; some can consume the already-read locked snapshot.

**Remedy:** retain a bounded parsed snapshot/read result from the held file for decision and metadata preservation inside the same transaction. Keep fresh identity verification when a pathname can have changed; do not cache pre-lock decisions across races. Preserve memory permits for both bytes and parsed metadata. Target format-upgrade tests that replace the pathname during each phase.

### 15. Settings initialization rereads one INI through many narrow helpers

**Functions:** `history_settings.cpp:167 ReadCategorizedInt`, `:196 ReadCaptureSpeed`, `:238 ReadEnrichmentFallbacks`, `:250 ReadWindowsPlaybackFallback`, `:260 ReadAutoSkipDownloaded`, `:270 ReadNeverPause`, `:283 ReadBlockUpdates`, `:294 ReadLogLevel`, `:409 ReadRestartOptions`, `:439 ReadMatcherOptions`, `:539 InitSettings`.

**Evidence:** each helper independently calls settings_file::Read on the same path; ReadCategorizedInt itself rereads for each filename/category setting. Initialization also mixes those snapshots with Win32 profile reads. This is startup-only overhead and can observe different snapshots if the file changes mid-initialization.

**Remedy:** read one startup text snapshot and use text-based readers; factor the already-existing ReadRestartOptionsText/ReadMatcherOptionsText pattern. Preserve intentional Win32 legacy integer behavior, canonical-section precedence, duplicate opt-in rejection, encoding/migration handling, and re-read-at-persistence so external edits are not overwritten by the startup snapshot.

## Small removals and optional cleanups

- `audio_history.cpp:448 Directories`: exact two-call wrapper around EnsureDirectory; inline callers and remove the name. No separate behavior to preserve.
- `audio_history.cpp:4126 NeedPlaybackIdentity`: ignores Settings and returns `discovery==Found`; remove unused preference argument or inline the predicate. Tests at `windows_playback_diagnostics_test.cpp:88` currently exercise this seam; move them to actual optional-hook policy before removing it.
- `history_settings.cpp:628 Set`: `key` is explicitly unused; remove it from this private helper and its setters. Persistence already maps settings in PersistSettings.
- `async_log.cpp:81 Take`, `:136 WriteRecord`: production-unused wrappers retained for tests that include the `.cpp`; update those tests to exercise TakeBatch/WriteBatch and remove wrappers, or put adapters in the test TU. Confirmed callers are windows_networking_log_test/windows_renderer_privacy_test, not native production code.
- `async_log.cpp:175 WriteCappedLog`: `tail.substr(boundary+1)` creates another large allocation during rotation. Write from an offset/string_view or erase in place. Keep complete-line retention, cap, and failure containment.
- `audio_history.cpp:2045 PublishUnmatched`: computes ArchiveHash over the same audio once for metadata and again for the commit marker; compute once per immutable publication artifact and reuse. Do not remove verification of actual existing files before acknowledging/reaping an older attempt.
- `decoder_ownership.cpp:344 TryBind`, `:361 BindCapture`, `:398 BindCaptureExact`: repeated final bound-state writes can share a private commit helper after policy checks. Their policy branches are not duplicates and should remain distinct.

## Verified keep rationale by subsystem

These are retained functions/guards, not merely “no issues found.” Every remaining in-scope body was reviewed; the accompanying machine inventory contains individual signatures/ranges.

- **Compressed/source formats:** `aac_container.cpp` primitive bounds helpers, Boxes/One/Children, Configuration/ReadConfig, Inspect, ReadTags/MetadataMovie, TimedMovie, Write/ReplaceMovie, PlanMux/EstimateAacConstructionMemory, TagAacM4a/MakeAacM4a; `aac_stream.h` accessors; `flac_history_core.cpp` ParseFlacStreamInfo/Metadata/FlacCoverage::Frame/Fields/IsSyncedLyrics/TagFlac. Retain exact ASC, packet offsets/timing/priming checks, unsupported/invalid vs resource failure distinction, output alias checks, unknown metadata preservation, output inspection and checked construction budgets. The conservative AAC 32× moov reserve follows simultaneously live boxing/copies; reduce only after eliminating/measuring those allocations. The separate compressed-source proof and decoder-output coverage answer different questions.
- **Artwork/cache:** `artwork_image.cpp` PngAllocations/PngMalloc/PngRealloc/PngFree/NormalizeArtworkImage; `artwork_reply.cpp` JSON/reference parsing; `cached_artwork_windows.cpp` file/index/image scans; remaining album artwork fetch/validation/revision functions. Keep allocation tracking around stb unwinding, decoded dimension/pixel bounds, RIFF length/animation rejection, post-conversion image validation, fixed-host URL checks/no redirects, and bounded cache/file/network operations. These protect different inputs; do not replace them with one generic “valid bytes” predicate.
- **Metadata/connection:** remaining cached_metadata protobuf/Snappy/LevelDB helpers and selection/merge logic; cached_metadata_windows enumeration/fallbacks; all connect_message.cpp parser/summary functions and connect_messages_windows.cpp queue/thread/status handlers. Keep maximum blocks/expanded sizes, CRC and sequence checks, exact embedded GID/type matching, tombstones, staged-output rollback, bounded latest-state delivery and diagnostic redaction. Optional diagnostic sanitization catches allocation failures in networking_summary.h; the suspected noexcept-termination issue was checked and rejected.
- **Ownership, auto-skip and playback:** all decoder_ownership owner/playback/capture lookup, transition, reset and evidence functions; auto_skip MusicUri/PlaybackInstance/ShouldAutoSkipDownloaded/QueueAutoSkipRequest/ReadAutoSkipRequest; client_playback ParseClientActiveWithoutUri and parser rejection gates; decoded_output_speed, capture_speed and download_policy helpers. Keep bounded 32-capture/16-owner/16-playback linear tables; a hash map adds allocations without established value here. Exact native identity, observed decoder output, complete compressed source, weak timing uniqueness, explicit controls, successor ambiguity and display-only quality are intentionally distinct. Auto-skip requires current playback identity and trusted quality.
- **Audio hook/ingestion:** audio_history Now/Log/DecisionLog/timing scopes, QueueGuard, queue loss aggregation, MP3 probe/generation/slot helpers, native discovery/adapters, FLAC/AAC/Ogg hooks, AdvancedSpan, decoded activity coalescing/queue, identity bridge callbacks/reset, codec Process* and reconciliation functions. Keep allocation-free bounded callback admission, same-context/generation loss isolation, producer causal sequence, native return/LastError preservation, span bounds, callback quiescence and source-specific restart checks. A raw decoder observation is not the same as an accepted capture event; diagnostics and capture have different queues and omission semantics.
- **Audio matching/retention:** Capture accessors, codec final validators/cache, CaptureBuffers retain/lost evidence methods, ApplyCaptureQueueLosses, interstitial/successor helpers, Begin/CompleteCaptureOwnership, BindReady/BindDirectReadyCapture, ObserveRecordingMetadata, RememberBoundCaptureIdentity, BoundPlaybackQuality, Report* helpers, AssociateRetainedRecordings, ScheduleStreamPublications, Worker. Keep competitors that are incomplete/invalidated when deciding uniqueness, retired evidence, revalidation when metadata duration changes, exact/native vs weak binding, and count-based retirement. Similar-looking fallback checks protect against false association; avoid a single permissive “match” predicate. A module split around queue ingestion, capture store/matcher and publication is useful later, but extracting functions alone does not solve the measured/reviewed repeated work above.
- **Publication:** audio_history catalog/tag identity helpers, destination permits/resolution, Publish/PublishUnmatched, archive reservation/write/verify helpers, SaveWorker, QueueRecording and cancellation; existing_quality read/memory scopes, directory listing/recovery/quality resolution; all file_publication staging/handle/receipt/rename/metadata helpers. Keep no-overwrite final rename, held identity checks, reserve-before-allocation, committed-before-ack receipts, verified-only cleanup and destination revision permits. A retry after commit must reconcile ownership rather than overwrite another file. Distinct publication outcomes encode retry/resource/cancel behavior and should not collapse to bool.
- **Settings/UI/startup:** history_settings category/migration/options/persistence/get/set/path/picker functions; cef_menu_abi Methods/Reference and cef_menu_capability helpers; collector_config; floggfy_launcher. Keep generation acknowledgment separate from write attempts, restart-only settings independent of runtime capture epochs, canonical migration defaults and reference transfers. CEF adapters centralize ABI slot knowledge and transfer ownership; replacing their short named wrappers with raw indexes would remove useful type/policy boundaries. Launcher closes successful process/thread handles.
- **Diagnostics and hook lifecycle:** remaining async_log producer/batch/fair-lane/cap functions; connectivity_diagnostics, diagnostic_activity, hook_diagnostics, hook_init_state, hook_rollback, hook_startup_batch, hook_startup_minhook; bounded_queue. Keep try-lock producers, per-lane omission evidence, enqueue timestamps, batch draining, UTF-8-safe truncation, callback counter ownership, retry/unsupported state distinction, bounded startup groups and preservation of trampolines after partial activation. Thread/resource lifetime choices match the pinned injected module; absence of a generic stop destructor is not evidence of a leak to “fix.”

## Suggested execution order

1. Reservation ownership + release-audit lock scope (one tightly tested change).
2. Slot-length copies and filename-check early dedup (small, measurable independent changes).
3. Constant cache-query predicate + dual-query traversal + transition parser extraction.
4. Per-capture budget deltas and diagnostic dirty-state caching only after adding structural work counters.
5. File snapshot reuse / artifact representation / module extraction separately, preserving existing race and memory-bound tests.

No live Spotify session, restart, configuration mutation or publication was performed. Only the temporary allocation probe was executed; this review does not claim the product test suite was run.
