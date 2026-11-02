# Capture Failure Log Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit a short cache-failure activity message containing the failed song's identity while retaining technical evidence in diagnostics.

**Architecture:** Snapshot artist/title into the exact bound capture during playback association. Format user-facing and diagnostic records separately at final release.

**Tech Stack:** C++17, Win32 native tests

**Spec:** `docs/superpowers/specs/2026-11-02-capture-failure-log-design.md`

## Global Constraints

- Do not change capture ownership, validation, retention, or publication decisions.
- Never attribute a delayed failure using the currently playing media.
- Keep detailed correlation evidence diagnostics-only.

## Review Focus

- Delayed cleanup after a new song starts must name the old bound song.
- An unbound capture must use `unknown track`.
- Artist/title conversion must remain bounded by the existing media limits.
- All final validation classifications must remain unchanged.
- Debug-disabled operation must still emit one compact failure activity record.

---

### Task 1: Attribute compact cache failures

**Files:**
- Modify: `native/audio_history.cpp`
- Test: `tests/windows_playback_diagnostics_test.cpp`

**Interfaces:**
- Consumes: exact ownership binding and the current `Media` associated with its listen generation.
- Produces: a capture-owned display identity and separate compact/detail release records.

- [ ] **Step 1: Write the failing regression**

Create a bound old-song capture, advance current playback to a different song, release the old capture, and assert the activity message is `incomplete cache | Old Artist - Old Title`; add an unbound case asserting `unknown track`.

- [ ] **Step 2: Run the focused Windows playback diagnostic test**

Expected: FAIL because final release currently emits the verbose message without a song identity.

- [ ] **Step 3: Implement capture identity and split logging**

Add bounded identity storage to `Capture`, update it only for exact generation bindings, emit the compact failure through `LogEvent`, and emit technical detail through diagnostic logging.

- [ ] **Step 4: Run focused and full verification**

Expected: Windows playback diagnostics, portable suite, diagnostics suite, and production build pass.

- [ ] **Step 5: Commit and update PR #21**

Commit the code/tests/docs, run the privacy scan, push the existing PR branch, and replace the pre-release assets only after verification.
