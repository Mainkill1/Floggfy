# General Log Level Design

## Intent

Replace the general `Log` INI switch with one readable, cumulative `LogLevel`
policy. `DebugLog` is removed because it only acted as a parent gate for other
logging. Keep ordinary logs useful at low levels, make diagnostic collection an
explicit highest-level choice, and timestamp every record in the user's local
time.

## Behavior

`LogLevel` accepts exactly four values:

- `0`: no activity, warning, failure, or diagnostic logging.
- `1`: completed-track outcomes, warnings, and failures.
- `2`: level 1 plus track starts, upgrades, metadata updates, skips, renames,
  and other user-visible capture lifecycle actions.
- `4`: level 2 plus the main diagnostic record lane.

Missing or invalid values normalize to level 1. The logger enforces the level
centrally from an explicit event classification; it never infers importance
from message text.

Diagnostic output is separated by purpose:

- level 4 writes ordinary diagnostic events to `Floggfy-diagnostics.log`;
- `DebugConnect=1` permits sanitized Connect/control observations in the main
  diagnostic file at level 4;
- `DebugNetworking=1` writes sanitized networking observations to
  `Floggfy-networking.log` at level 4;
- `DebugHookDiagnostics=1` writes callback and stall observations to
  `Floggfy-hook-diagnostics.log` at level 4.

Networking and hook diagnostics are independent opt-ins. Neither depends on the
other, and neither can enable output below level 4.

The generated INI contains `LogLevel=1` and no longer contains `Log` or
`DebugLog`. Existing installations receive a one-time compatibility migration:

- legacy `Log=0` becomes `LogLevel=0`;
- legacy `Log=1` with `DebugLog=1` becomes `LogLevel=4`;
- other enabled legacy logging becomes `LogLevel=1`.

After migration, the obsolete keys are removed so there is one authoritative
setting.

## Timestamp Format

All four log files use local ISO 8601 time with a numeric UTC offset, for
example `2026-11-03T09:42:18-08:00`. Diagnostic records retain milliseconds.
Conversion happens in the logger from the observation timestamp, so call sites
do not perform clock or timezone work.

## Implementation Approach

The selected approach extends the existing producer-owned log classification
with an explicit minimum level and filters in the shared asynchronous logger.
This is preferred over parsing event names, which is fragile, and over replacing
every logging call with separate logger APIs, which would create unnecessary
surface area.

Settings parsing, persistence, and tests cover exact accepted values, invalid
input, legacy migration, and independent diagnostic gating. Logger tests cover
each level, routing to four separate files, and local timestamp formatting.
Verification remains limited to the changed logging and settings paths, as
requested.

## Non-goals

- No raw Spotify payloads, credentials, URLs, media bytes, or device identifiers
  are added to logs.
- Log file names, size caps, queue bounds, and omission handling do not change.
- Log levels do not alter capture, publication, metadata, or playback decisions.
