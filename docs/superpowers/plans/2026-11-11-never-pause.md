# Never Pause Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Add the requested Never Pause toggle and tri-state INI control.
**Architecture:** Reuse the settings generation and renderer heartbeat, without capture epoch changes or new polling threads.
**Tech Stack:** C++17/Win32/CEF and JavaScript.
**Spec:** docs/superpowers/specs/2026-11-11-never-pause-design.md

## Global Constraints

- NeverPause=-1 default hides/disables; 0 visible/off; 1 visible/on.
- Preserve existing INI wording and all earlier changes. No commits or publication.
- Keep test files/logs isolated from the user's settings and media.

## Review Focus

- Hidden stale-menu commands cannot enable playback control.
- Remote/unknown targets and playing clients get no play commands.
- Missing/invalid/duplicate values cannot enable the feature.
- Rejected or unresolved commands cannot flood the player.
- Toggling must persist without resetting current captures.

### Task 1: Settings, menu and renderer control

Files: native/history_settings.{h,cpp}, native/to_disk_menu.cpp, native/collector_config.h, native/metadata_collector.js, SpotifyHistory.ini. Tests: tests/never_pause_collector_test.js, tests/windows_settings_test.cpp, tests/windows_menu_quality_test.cpp, tests/windows_pr13_settings_test.cpp; integrate into existing test wrappers.

- [x] Add failing paused-player test and observe missing resume dispatch.
- [x] Add tri-state defaults/parse/persistence, visibility and menu-toggle checks.
- [x] Implement integer Settings::never_pause and SetNeverPause(bool), strict reader, generated/bundled default and collector boolean; preserve capture epoch.
- [x] Add menu entry with unused CEF command ID; block hidden commands.
- [x] Add bounded renderer Resume/Play dispatch after timeline observation, independent of metadata availability.
- [x] Run focused checks, portable suite and Windows suite; build and review.
- [x] Refresh documented behavior, package and verify checksums. Keep the user's active setting disabled unless explicitly selected.
