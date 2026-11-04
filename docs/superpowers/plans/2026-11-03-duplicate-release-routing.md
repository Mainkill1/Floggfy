# Explicit UID Collision Fallback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a root `Duplicates` fallback only after the existing resolver proves an explicit Spotify track-URI collision.

**Architecture:** Keep the current resolver and all canonical behavior intact. Extend its result with non-authorizing evidence that a different explicit UID occupied a candidate, add a pure duplicate-path builder, and let existing publication/update/menu callers rerun the same resolver in that namespace only when no exact current recording was found.

**Tech Stack:** C++17, Win32 wide-path/file APIs, existing recording inspectors, destination resolver, and native test harnesses.

**Spec:** `docs/superpowers/specs/2026-11-03-duplicate-release-routing-design.md`

## Global Constraints

- Existing canonical resolution, conversion, matching, and URI-less legacy behavior remain unchanged.
- Only two present, valid, different Spotify track URIs trigger duplicate routing.
- Duplicate routing never authorizes replacement, metadata edits, deletion, or Auto Skip.
- Preserve the complete track identifier in the duplicate filename.
- Podcasts retain their existing routing.

## Review Focus

- URI-less legacy files and invalid containers must keep current behavior rather than being redirected.
- An exact current-track match must win even if another candidate reported a different UID.
- A conflict in one codec must redirect a new publication in another codec.
- Metadata checks and current-song lookup must rediscover the exact duplicate path.
- Long Unicode titles must preserve the complete identifier and component budget.

---

### Task 1: Pure duplicate naming

**Files:**
- Modify: `native/library_layout.h`
- Modify: `native/library_layout.cpp`
- Test: `tests/library_layout_test.cpp`

**Interfaces:**
- Produces: `DuplicateOutputPath(...)`, returning empty unless `Catalog::spotify_uri` is a valid Spotify track URI.

- [ ] Write failing path tests for hierarchy, full identifier, invalid/missing URI, and long Unicode titles.
- [ ] Run the focused portable test and confirm the API/behavior is absent.
- [ ] Implement the minimal bounded path builder without changing `OutputPath` or `RelativePath` results.
- [ ] Run the focused portable test and confirm it passes.
- [ ] Commit the task.

### Task 2: Non-authorizing UID conflict evidence

**Files:**
- Modify: `native/existing_quality.h`
- Modify: `native/existing_quality.cpp`
- Test: `tests/windows_collision_reuse_test.cpp`
- Test: `tests/windows_directory_lookup_test.cpp`

**Interfaces:**
- Produces: `RecordingDestination::explicit_uid_conflict` while preserving current state/path/recording selection.
- Consumes: successfully parsed `SPOTIFY_URI` fields and current `Catalog::spotify_uri`.

- [ ] Write failing tests for explicit mismatch, same UID, missing UID, invalid file, read error, and an exact match after an earlier mismatch.
- [ ] Run focused Windows tests and confirm conflict evidence is absent.
- [ ] Accumulate mismatch evidence during the existing bounded candidate scan without changing its decisions.
- [ ] Run focused Windows tests and confirm current decisions plus new evidence pass.
- [ ] Commit the task.

### Task 3: Publication fallback

**Files:**
- Modify: `native/audio_history.cpp`
- Test: `tests/windows_quality_observer_test.cpp`
- Test: `tests/windows_aac_capture_test.cpp`

**Interfaces:**
- Consumes: `DuplicateOutputPath(...)` and `explicit_uid_conflict` from Tasks 1-2.
- Produces: cross-format duplicate routing through the existing resolver and publication pipeline.

- [ ] Write failing integration tests for same-format reuse and cross-format conflict redirection.
- [ ] Run focused Windows tests and confirm publication remains canonical.
- [ ] Add the smallest shared resolution helper that reruns existing format checks in `Duplicates` only after explicit conflict and no exact match.
- [ ] Run focused Windows publication tests and confirm they pass.
- [ ] Commit the task.

### Task 4: Existing-file and folder lookup fallback

**Files:**
- Modify: `native/audio_history.cpp`
- Modify: `native/song_destination.cpp`
- Test: `tests/windows_quality_observer_test.cpp`
- Test: `tests/windows_song_destination_test.cpp`

**Interfaces:**
- Consumes: Task 3's duplicate resolution rule.
- Produces: duplicate-aware metadata update, Auto Skip, filename conversion, and current-song folder action.

- [ ] Write failing tests exercising each consumer against a previously saved duplicate recording.
- [ ] Run focused Windows tests and confirm the duplicate is not found.
- [ ] Reuse the exact fallback rule after canonical resolution in both workers.
- [ ] Run focused Windows integration tests and confirm they pass.
- [ ] Commit the task.

### Task 5: Verification and maintained documentation

**Files:**
- Modify: `docs/capture-invariants.md`

**Interfaces:**
- Consumes: completed Tasks 1-4.
- Produces: maintained invariant documentation and complete test evidence.

- [ ] Document the final explicit-UID fallback and its non-goals.
- [ ] Run the complete portable suite.
- [ ] Build and run all changed Windows test targets on the WSL Windows host.
- [ ] Inspect the final diff for private paths, logs, credentials, build artifacts, and unrelated changes.
- [ ] Commit the task.

