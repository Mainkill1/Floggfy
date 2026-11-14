# FLAC repeated input recovery implementation plan

> **For agentic workers:** Use the existing isolated worktree. Preserve prior uncommitted fixes. User authorized implementation and will validate the candidate live.

**Goal:** Recover the observed repeated FLAC input without accepting incomplete or conflicting audio, and retain useful matcher diagnostics without unchanged snapshot floods.

**Architecture:** Keep compressed source parsing independent of native playback frame callbacks and metadata matching. Normalize verified repeated frame ranges within one capture, then use the existing strict independent validator and native identity matcher. No cross-capture assembly, timing changes, new configuration, or durable storage.

**Tech stack:** Existing C++17 Windows DLL, portable parser tests, Windows playback diagnostics harness, local frozen stream dumps.

## Evidence and constraints

- Bumblebeez Intro: repeated frame positions stop contiguous coverage. Five later starts retire the still-incomplete capture.
- The consumed-input dump reconstructs 470 unique frames / 1,921,584 samples with exact STREAMINFO PCM MD5. Production uses a different read hook; exact released-buffer bytes are unavailable.
- 13 clean files passed decoding and identity validation; preserve that path.
- Keep TimeOffsetToleranceMs=10000, MaxBufferedMiB=512, and five-start unmatched retirement.
- Reject gaps, conflicting duplicates, invalid checksums, incompatible format, and ownership loss. Do not combine different capture lifetimes.
- Recovery storage must be bounded/accounted. Avoid an additional full audio copy.

## Tasks

- [x] Add failing parser tests for repeated frames, partial read followed by verified reread, missing frames, conflicting duplicates, and chunk boundaries.
- [x] Implement a capture-only normalizer in recording_file.h/.cpp using the existing FLAC frame parser and bounded same-source evidence; leave strict file inspection independent.
- [x] Integrate normalization and explicit reader reasons into ProcessFlacCapture; verify final validation, sample-zero lifecycle, native matching, and memory accounting in the Windows harness.
- [x] Diagnose and correct unchanged matcher snapshot emission; preserve changed candidate evidence and terminal reasons, with a regression covering repeated evaluation.
- [x] Replay the frozen source through actual new code, independently decode and verify PCM MD5, run relevant suites, review, build, and install the candidate DLL for the user's validation.

## Review focus

1. Corrupt bytes cannot be silently skipped to manufacture completion.
2. Duplicate positions with different frame bytes must block publication.
3. Partial callback chunks and repeated sample zero cannot corrupt lifetime ownership.
4. Recovery overhead remains bounded and allocation failure does not lose another capture.
5. Diagnostic suppression must preserve every changed decision and final outcome.
