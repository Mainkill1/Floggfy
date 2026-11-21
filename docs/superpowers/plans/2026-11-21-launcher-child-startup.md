# Launcher Child Startup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Load `Floggfy.dll` into every covered Spotify child before its entry point executes in launcher mode.

**Architecture:** Extract the launcher's stopped-process loader into shared tooling, then extend the existing `CreateProcessW` hook to apply it to every spawned child according to `HookAllSubProcesses`. The hook continues to own crash URL rewriting and becomes recursively active when each child loads Floggfy.

**Tech Stack:** C++17, Win32 process/debug APIs, MinHook, MinGW-w64 Windows integration tests

**Spec:** `docs/superpowers/specs/2026-11-21-launcher-child-startup-design.md`

## Global Constraints

- Use `HookAllSubProcesses=0/1/2` for off, launcher-only, or every-load-method coverage.
- Cover every child created through the hooked `CreateProcessW`; do not scan processes.
- Load and verify Floggfy before child application entry.
- Preserve the caller's suspended/running intent and all unrelated CreateProcess arguments.
- Keep one MinHook detour on `CreateProcessW`.
- Fail closed for a covered child whose DLL load cannot be verified.

## Review Focus

- Child loading does not depend on executable names, paths, or command-line parsing.
- Modes 0, 1, and 2 retain their documented load-method boundaries.
- An originally suspended child remains suspended after Floggfy is loaded.
- A load failure terminates the covered child, closes Floggfy-owned handles, and returns a stable native error.
- Crash-report URL filtering still works when child loading owns the same process hook.

---

### Task 1: Shared pre-entry loader

**Files:**
- Create: `native/tooling/process_startup_loader.h`
- Create: `native/tooling/process_startup_loader.cpp`
- Modify: `native/tooling/startup_launcher.cpp`
- Test: `tests/windows_startup_test.cpp`

**Interfaces:**
- Produces: `startup::StopAtEntry(PROCESS_INFORMATION&) -> bool`
- Produces: `startup::LoadStoppedProcess(PROCESS_INFORMATION&, const std::wstring&, std::wstring&) -> bool`

- [ ] Add a failing startup fixture assertion that calls the shared API and verifies `Floggfy.dll` exists before entry.
- [ ] Run the Windows startup test and confirm it fails because the shared API does not exist.
- [ ] Extract the existing entry-stop and DLL-load implementations without changing launcher behavior.
- [ ] Run the startup test and existing startup failure cases.
- [ ] Commit the shared loader extraction.

### Task 2: Recursive covered-child launch hook

**Files:**
- Create: `native/tooling/spotify_child_process.h`
- Create: `native/tooling/spotify_child_process.cpp`
- Modify: `native/telemetry/telemetry_crash.h`
- Modify: `native/telemetry/telemetry_crash.cpp`
- Modify: `native/version_proxy.cpp`
- Modify: `build-native.sh`
- Test: `tests/windows_spotify_child_process_test.cpp`
- Test: `tests/windows_telemetry_crash_test.cpp`
- Modify: `tests/runners/test-native-windows.sh`

**Interfaces:**
- Produces: child configuration derived from the loaded module path and mode 0/1/2.
- Consumes: `startup::StopAtEntry` and `startup::LoadStoppedProcess`.
- Produces: child launch status counters for requested, loaded, failed, and unsupported-debug launches.

- [ ] Write a real-process failing test where a parent creates `Spotify.exe`, the child checks Floggfy before entry, and a descendant repeats the check.
- [ ] Add failing cases for preserved suspension, arbitrary child names/paths, mode 0/1/2, load failure cleanup, and null application names.
- [ ] Run the focused test and confirm failure because the current hook forwards children without loading Floggfy.
- [ ] Implement the mode gate without executable-name or path filtering.
- [ ] Extend the existing `CreateProcessW` detour to stop, load, verify, and resume covered children while retaining crash URL rewriting.
- [ ] Activate the hook whenever launcher child coverage or crash blocking requires it.
- [ ] Run the child and crash-hook tests until green.
- [ ] Commit recursive child coverage.

### Task 3: Packaging, diagnostics, and live validation

**Files:**
- Modify: `README.md`
- Modify: `AUDIO-HISTORY.md`
- Modify: `native/version_proxy.cpp`
- Modify: `package-release.py` if the launcher archive layout needs adjustment
- Test: `tests/package_release_test.py`

**Interfaces:**
- Consumes: child launch counters from Task 2.
- Produces: per-process diagnostics that identify recursive launcher coverage and failures.

- [ ] Add diagnostics for launcher child coverage and update launcher documentation.
- [ ] Run packaging tests and inspect the launcher archive contents.
- [ ] Run the full Windows and portable suites.
- [ ] Install the development launcher in an isolated Spotify fixture and repeat the Fiddler switch-on/switch-off capture.
- [ ] Verify every sending Spotify PID loaded `Floggfy.dll` and the enabled switch produces zero Fiddler requests.
- [ ] Commit documentation and validation updates.
