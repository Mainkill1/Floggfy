# Unresolved Duplicate Routing Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan.

**Goal:** Keep new unresolved collision files beneath Duplicates and identify missing track IDs in their filenames.

**Architecture:** Extend RC6's shared duplicate path builder with an explicit no-id hash suffix. Detect canonical collision vacancies in publication and lookup, retaining all matching-file and publication safety checks.

**Tech Stack:** C++17, MinGW, WSL/Windows filesystem integration tests.

**Spec:** ../specs/2026-11-11-unresolved-duplicate-routing-design.md

## Global constraints

Preserve canonical matches and existing legacy homes. Do not move or delete user recordings. Hashes never authorize reuse. Keep the 100-unit duplicate filename stem limit.

## Review focus

Missing/malformed IDs; unreadable and invalid occupants; same-recording replay and quality upgrades; alternate codecs; menu and track-start discovery.

## Task 1: Resolve collisions consistently

- [x] Add real Windows regression cases to `tests/windows_directory_lookup_test.cpp` covering missing IDs on either side, malformed IDs, retained bytes, replay, upgrades and Song discovery.
- [x] Run `bash test-directory-lookup.sh --duplicate-routing`; observe the routing assertion fail on RC6.
- [x] Extend `native/library_layout.cpp` with bounded no-id names and canonical collision detection in `native/audio_history.cpp` and `native/song_destination.cpp`.
- [x] Update layout expectations and verify focused regressions, the portable suite, full Windows suite and production build.
- [x] Update README and VALIDATION with the policy and observed verification, retaining live-validation limitations.

## Execution ledger

- Fresh checkout on `fix/rc6-duplicate-routing` at RC6 tag `7b825e3`.
- RC6 portable suite passed. Published RC6 DLL SHA-256 matches its manifest.
- Local toolchain required user-space MinGW installation; POSIX runtime executables could not start (exit 53). Switched to Windows-thread MinGW; RC6 AAC publication gate passed.
- Initial Windows regression failed at the assertion that the saved path starts with the root Duplicates hierarchy. The initial implementation passed five missing/malformed-ID scenarios, including locked-file deferral, byte preservation, replay, upgrades, Song discovery and conversion.
- Independent review identified a late-ID-enrichment gap: valid-ID lookup missed an existing no-id path. Added a separate failing regression before implementing a bounded lookup-only alias. Differing nonempty malformed URIs remain conservative and are not merged by text.
- The expanded regression passed, including enriched-ID replay, Song lookup and numbered-name conversion, and refusing a new no-id write when all valid-ID suffixes are occupied. Final independent static review found no remaining important defects.
- A full Windows run exposed an old publication-log fixture expecting the in-album collision path. Updated it to require Duplicates while retaining byte-preservation and absent-old-path assertions. The complete corrected Windows suite passed, exit 0. Final portable suite, production build, Windows layout executable, installed Spotify DLL discovery, and 50,000-mutation ASan/UBSan campaign also passed.
- Work and local packages remain on the RC6-based branch for review. Spotify was not modified, restarted or made to play; live end-to-end saving remains untested.
