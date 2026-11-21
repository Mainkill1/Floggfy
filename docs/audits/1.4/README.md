# Floggfy 1.4 function review

The review covers every first-party definition in the v1.4 inventory. Each row has a decision and the reason for it. Related operations such as callback adapters, fixture stubs or codec checks share a note when they have the same constraint. The supplemental inventory covers definitions added or renamed on PR #34.

| Decision on v1.4 runtime definitions | Count |
|---|---:|
| Keep, with a reason | 1,573 |
| Implemented a change | 170 |
| Removed or folded into another function | 31 |
| Open, with a proposed change | 63 |
| Measure before changing | 90 |
| **Total** | **1,927** |

The 63 open entries include repeated destination-path searches and duplicate artwork URL checks. The 90 measurement entries include capture-budget scans and peak memory during large metadata upgrades. Some rows describe the same change across several callers. Counts refer to definitions, so they cannot be used as counts of independent fixes.

## Implemented and checked

| Area | Change | Verification |
|---|---|---|
| Capture memory | Transfer the retained buffer’s charge to the release audit and release its final reservation. The background worker reserves space before it copies and validates the audio. | Exact-fit codec fixtures, full audit queue, allocation failure, and capture ingestion during a paused background audit. |
| Capture and dump queues | Copy the event header and its occupied bytes while holding the queue lock. | Mixed control, small, and full payloads, slot reuse, and stream dump replay fixtures. |
| Buffered codec validation | Read AAC directly from capture blocks. Ogg pages use the same storage unless a page crosses a block boundary. | On a 25,920,058-byte Ogg fixture, 1,279,800 bytes needed a contiguous copy. AAC validation used no intermediate chunk copy on a 25,800,033-byte fixture. Both codecs retained their checksum and completeness checks. |
| Offline metadata lookup | Collect the known track and album during the same table or write-ahead log traversal. When the track supplies the album ID, the second lookup still uses the remaining byte budget. | A Windows lookup of a compressed table and a write-ahead log went from two parses of each file to one, including one Snappy expansion. Per-ID limits, deletion precedence, and malformed-file rollback are covered by fixtures. |
| Saved column | Retry failed SQLite reads. Reuse the date sort until its inputs change and request missing or expired dates in batches of up to 256. | Locked SQLite database, unchanged playlists, date changes, expiry, and pagination. |
| Metadata and paths | Skip tag and artwork preparation for an already queued filename check. Reuse destination paths when their inputs match. Compare cache keys in place. | 1,000 unchanged destination submissions build no paths. For 10,000 rejected metadata keys, heap allocations fell from 20,000 to zero. |
| Publication | Move unmatched file staging and verification into the file publication module. Hash the unmatched audio once per attempt and append artwork directly to the existing publication key. | Windows publication fixtures, including occupied paths, retries, and metadata changes during a save. |
| Logging and wrappers | Skip telemetry decoding when the dump file is full, trim the activity log in place, and retire three wrappers used only in tests. | Log cap and deletion recovery, log rotation, and direct batch and MP3 inspection tests. |
| INI editing | Borrow each line and key while setting or erasing a value. Skip 12 duplicate legacy-key erasures during migration. | A Windows allocation probe with 1,000 comment lines went from 2,011 to zero allocations for Set and from 3,012 to zero for Erase. Both revisions produced identical INI contents. Section migration, Unicode reload and strict-option fixtures passed. |
| Metadata and capture | Copy only occupied metadata payload bytes under the queue lock. Skip capture-budget accounting on ignored events. Reuse measured Ogg and SpAC page layout and avoid building the AAC metadata box twice. | Portable Ogg, SpAC and M4A checks, Windows AAC and metadata liveness fixtures, and 50,000 capture ownership fuzz cases. |
| Artwork | Retire the artwork singleton after tracing all writers. Use the recording's pinned artwork and metadata revision. | Windows fixtures publish an actual image, deduplicate a repeat, and update embedded FLAC and Ogg covers. |
| Staging collisions | Keep the exclusively created file handle through publication or cleanup. A failed creation previously deleted an occupied sibling by pathname. | The Windows first-save collision reproduced the deletion. Normal and metadata-only saves now preserve the existing bytes. Interruption and destination-race checks also passed. |
| Windows diagnostics fixture | Create the two synthetic architecture images inside a fresh test-owned directory. | The previous fixture deleted adjacent sentinel files despite reporting success. Both sentinels now survive byte-for-byte. |

The detailed reviews below retain the original findings. The CSV dispositions record their current status.

## Source and scope

The inventory contains 3,544 entries across 233 files, including 1,927 runtime entries and 1,617 test and tooling entries. Counts include compiler-special members and macro-generated forwarders. The inventory uses the v1.4 release tree (`1e3d67a90442b46126fcf35cd6a79eca66b704e6`) and paths after the test-runner move. Each CSV row identifies a definition by file, one-based line and column, including overloads, inline methods, nested callbacks and lambdas. Filter by disposition to find proposed changes. The detailed reviews also cover surrounding callers and related functions.

- [Runtime function inventory](runtime.csv) covers first-party native code, headers, the five renderer scripts, the 17 assembly export forwarders and the 12 socket wrappers generated by the project macro.
- [Cleanup additions](cleanup.csv) covers 84 runtime and 53 test/tool definitions added or renamed in the cleanup. Its paths and one-based coordinates refer to commit `bb067f4`. Relocated test runners retain their baseline entries.
- [Test and tooling inventory](tests-tools.csv) covers test callbacks and fixtures as well as repository tooling. Scripts consisting solely of top-level statements have no function row.
- [Capture, buffering and settings review](capture-and-buffering.md) examines the capture and publication workers, memory accounting, codec handling and settings reads.
- [Saved, metadata and playback review](saved-metadata-and-playback.md) examines catalog queries, sorting, polling, format inspection and candidates for removal.
- [Settings, destinations and diagnostics review](settings-destinations-and-diagnostics.md) examines repeated work in the destination publisher, diagnostics, startup discovery and the menu.

Tree-sitter enumerated definitions in tracked C, C++, JavaScript, Python and Bash sources. Ctags provided a second check of named C/C++ functions, including alternate entry points compiled from conditional signatures. Compiler-defaulted/deleted members are labeled separately. The loop count is syntactic, excludes nested functions and does not count work inside called APIs. The call column is a sample of at most twelve distinct callees, not a call graph. Third-party implementations in `native/vendor/` and generated build output are outside this review. The CSV decisions reflect source and caller review through `bb067f4`. Filter the two release inventories by Open for concrete proposals or Measure for observations that need profiling. The supplemental inventory records its findings in the review note. Measurements in the table above apply only to the stated fixtures. Live Spotify playback latency was outside this pass.

## File boundaries and tools

Split `audio_history.cpp` by owned state and queue boundaries. Start with publication and retry scheduling, then capture ingress, then matcher decisions. Preserve lock ownership and the ordering of match, save, retry and release. Run the existing capture replay and memory-budget cases after each extraction. Leave `Worker` as the owner of event ordering. The detailed reviews identify smaller boundaries for the menu, metadata bridge and stream-dump writer.

For validation tools, both `validate_flac.py::validate` and `validate_showcase.py::validate` read a whole encoded recording into memory. Stream the container and update the hash incrementally if large-podcast validation is limited by peak RAM. Compare metadata, sample counts, hashes and error detection against the current tools. In `analyze_decoder_ownership.py::analyze`, an output or ambiguous binding can scan the capture history. An owner index and a maintained unresolved count could replace those scans. Verify the same violation messages on out-of-order, repeated and partially missing records before changing the analyzer.

The repository already has targeted Windows fixtures and replay runners. Keep these separate from production build dependencies and preserve the historical commands in dated validation records. Use the new paths under `tests/runners/` for current runs.

## Follow-up on telemetry, UI and tooling

The inventories above retain source coordinates from the revisions they reviewed. The [component map](../../wiki/README.md#component-map) points to the current directories.

| Operation | Before | After |
|---|---:|---:|
| Saved catalog lookup for one saved URI and one missing URI, SELECT executions | 3 | 2 |
| Telemetry option read, whole-file INI scans | 24 | 1 |
| Passive Saved refresh with 128 mounted rows, header layout probes | 129 | 1 |
| Same refresh, grid width reads | 129 | 2 |
| Native menu requests one audio label, labels built | 4 | 1 |
| Renderer resolves one relative URL each for fetch, beacon and XHR | 6 | 3 |

The SQLite fixture counted three prepared statements for the open catalog across repeated lookups and writes. It also exercised an exclusive lock, retry, pagination and save-root changes. The offline metadata fixture checked that reading an unrelated LevelDB table or log preserves previously collected records in place.

The telemetry fixture exercised 5,040 URL classifications with the same results. The Windows INI fixture retained strict duplicate handling and section precedence. Two embedded renderer bundles totaling 121,506 UTF-8 bytes round-tripped byte-for-byte through the Windows conversion. The directory race fixture covers a competing creator producing either a file or a folder. These are fixture measurements. Live playback and end-to-end UI latency were outside this pass.

## Repeated operations consolidated

| Repeated operation | Shared implementation |
|---|---|
| Reading fixed-width integers in media, artwork cache and telemetry payloads | [Binary tooling](../../native/tooling/binary.h) also shares the CRC-32 calculation used by PNG and gzip. |
| Encoding Ogg artwork and telemetry bodies, decoding padded artwork comments and renderer dumps | [Base64 tooling](../../native/tooling/base64.h) keeps padding and trailing-bit validation in one place. Ogg comments allow whitespace. |
| Validating UTF-8 and decoding hexadecimal escapes | [Text encoding tooling](../../native/tooling/text_encoding.h) serves metadata, artwork and telemetry with explicit text policies. |
| Checking the Spotify CDN URL in four places | [Artwork URL validation](../../native/media/artwork_url.h) serves the metadata parser, artwork replies, disk cache and downloader. |
| Marking a validated Windows file for deletion through its open handle | [Windows file tooling](../../native/tooling/windows_file_write.h) serves publication, recovery and queued-telemetry cleanup. |
| Publishing captured telemetry dumps and recording a successful decoder binding | The native and renderer dump paths share queue accounting. The three decoder binding paths share the state update. |
