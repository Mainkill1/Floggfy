# Capture Speed Startup Switch

## Purpose

Accelerated capture remains bounded to the supported choices and can be hidden
with one restart-only INI switch. The normal startup behavior exposes the menu.

## Configuration contract

- `CaptureSpeedLimit` is read from `[To Disk]` in `SpotifyHistory.ini` at
  startup. It is not exposed as a menu setting.
- A missing value defaults to enabled and is written as `1`.
- Exactly `0` disables acceleration and hides the Capture Speed submenu.
- Any valid nonzero integer enables acceleration and normalizes internally to `1`.
- Malformed, duplicate, negative, or padded values fail closed as disabled.
- Only the existing supported capture speeds (`1`, `2`, `4`, `8`, and `10`)
  remain selectable. The switch does not impose a numeric ceiling.
- The setting is restart-only. Runtime persistence must not overwrite an
  external edit before restart.

## Runtime contract

- `1x` is always available.
- An accelerated speed is available only when the switch is enabled, the speed
  is supported, and the decoded-output acceleration hook is active.
- If the switch is disabled, a saved accelerated `CaptureSpeed` falls back to `1x`.
- The menu is absent when disabled. When enabled, capability-unavailable entries
  are disabled, and the native setter enforces the same rule without the menu.

