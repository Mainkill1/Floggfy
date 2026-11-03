# Capture Speed Limit

## Purpose

Accelerated capture is an experimental, hidden feature. It must require an
explicit INI opt-in and must never exceed 10x.

## Configuration contract

- `CaptureSpeedLimit` is read from `[To Disk]` in `SpotifyHistory.ini` at
  startup. It is not exposed as a menu setting.
- A missing, malformed, duplicate, negative, zero, or one value disables every
  accelerated speed. The effective limit is `0`.
- Values from `2` through `10` are accepted as the ceiling.
- Values above `10` are clamped to `10`.
- Only the existing supported capture speeds (`1`, `2`, `4`, `8`, and `10`)
  remain selectable. A ceiling of `3`, for example, permits `2x` but not `4x`.
- The setting is restart-only. Runtime persistence must not overwrite an
  external edit before restart.

## Runtime contract

- `1x` is always available.
- An accelerated speed is available only when it is supported, does not exceed
  the configured ceiling, and the decoded-output acceleration hook is active.
- If the saved `CaptureSpeed` exceeds the startup ceiling, effective capture
  speed falls back to `1x`.
- The menu remains visible. Accelerated entries that are not available are
  disabled, and the native setter enforces the same rule even if invoked
  without the menu.

