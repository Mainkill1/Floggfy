# Capture Speed Startup Switch Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Gate accelerated capture behind a restart-only INI switch that defaults on and never changes the supported 10x maximum.

**Architecture:** Parse and retain a boolean visibility switch in the settings layer. Expose one `CaptureSpeedAvailable` predicate that combines supported speed, the switch, and live hook capability; use it in both the setter and menu.

**Tech Stack:** C++17, Win32 INI APIs, existing Windows unit harness.

**Spec:** `docs/superpowers/specs/2026-11-02-capture-speed-limit.md`

---

### Task 1: Define and enforce the settings contract

**Files:**
- Modify: `tests/windows_settings_test.cpp`
- Modify: `test-native-windows.sh`
- Modify: `native/history_settings.h`
- Modify: `native/history_settings.cpp`
- Modify: `SpotifyHistory.ini`

1. Add scenarios proving one/nonzero values enable acceleration, missing/zero
   disables it, malformed values fail closed, and legacy values impose no ceiling.
2. Run the focused Windows settings scenarios and observe the pre-change
   failure.
3. Parse `CaptureSpeedLimit`, store its effective boolean value, normalize the selected
   speed to `1x` when disallowed, and expose `CaptureSpeedAvailable`.
4. Make `SetCaptureSpeed` use the shared predicate and add the default INI key.
5. Re-run all Windows settings scenarios and require a pass.

### Task 2: Make the menu reflect the same rule

**Files:**
- Modify: `tests/windows_menu_quality_test.cpp`
- Modify: `native/to_disk_menu.cpp`

1. Add menu regressions proving zero hides the menu, the default exposes it,
   and a live hook enables every supported speed.
2. Run the focused menu test and observe the pre-change failure.
3. Use `CaptureSpeedAvailable` for initial and refreshed menu state and for the
   execution guard.
4. Re-run the focused menu test and require a pass.

### Task 3: Verify the branch

**Files:**
- Modify: `docs/wiki/settings.md` if the settings table needs the INI-only key.

1. Document the restart-only visibility switch at a high level.
2. Run the full portable suite and full Windows code-path suite.
3. Inspect the diff for duplicated policy, stale documentation, or unrelated
   changes.
4. Request one fresh whole-branch review and address any important findings
   with a failing regression first.

