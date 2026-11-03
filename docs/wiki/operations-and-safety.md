# Operations and safety

## Runtime pieces

Floggfy runs inside the Spotify process, so isolation matters more than convenience.
The main runtime pieces are:

- small hook callbacks that only copy bounded observations;
- one capture/correlation worker;
- one bounded publication worker;
- a metadata/enrichment worker;
- a Windows media-session reader; and
- asynchronous bounded logging.

The workers exchange owned data through bounded queues. Queue overflow is explicit
loss of evidence: affected active correlation is invalidated instead of silently
continuing with partial observations. Filesystem and network enrichment work never
runs inside an audio callback.

## Capability layers

Capabilities are activated independently where possible:

1. Required Ogg/FLAC discovery and hooks provide ordinary capture.
2. AAC discovery/hooks add optional AAC capture.
3. Decoded-output discovery and successful hook activation add ownership evidence,
   podcast MP3 input, and acceleration.
4. Client metadata injection provides the preferred timeline and enrichment.
5. Windows media sessions provide fallback playback observations only when the
   default-off `Windows Playback Fallback=1` INI option explicitly enables them.

Discovery success is not treated as activation success. If an optional hook cannot
be installed or enabled safely, it is removed and its feature is disabled. Ordinary
capture remains available at 1x when its required hooks are intact. Hook creation,
queued activation, rollback, and retained trampoline handling are in
[`audio_history.cpp`](../../native/audio_history.cpp) and the
[`hook_startup_*`](../../native/hook_startup_batch.h) helpers.

## Settings and live changes

`SpotifyHistory.ini` is loaded through
[`history_settings.cpp`](../../native/history_settings.cpp). The **To Disk** menu
updates the same settings through [`to_disk_menu.cpp`](../../native/to_disk_menu.cpp).
Important controls include:

| Control | Effect |
| --- | --- |
| Downloads | Master capture/publication switch |
| Save Location | Root of the managed library |
| FLAC / Ogg / AAC | Enable publication of each music format; AAC is INI-only by default |
| Podcasts | Enable native MP3 episode capture and the Podcasts layout |
| Lyrics / Metadata | Control enrichment and later metadata filling |
| Capture Speed | Select 1x or an available accelerated mode |
| Windows Playback Fallback | INI-only, default-off compatibility source when Spotify state is unavailable |
| TrackNumberInFilename | Add track prefixes to filenames |
| ConvertNumberedFilenames | Safely remove recognized old prefixes when numbering is disabled |

Settings carry a generation and capture epoch. Workers recheck them before commit;
turning a format off cannot authorize a queued publication from an older settings
generation.

## Failure policy

Floggfy prefers a missed save over a confidently wrong file. The main terminal
reasons are based on concrete evidence:

- invalid or incomplete container structure;
- decoder error, reinitialization, or bounded-memory failure;
- capture/decoded queue loss or capture-epoch replacement;
- contradictory decoder or playback ownership;
- manual seek/skip or incomplete listen coverage;
- more than one viable capture-to-listen association;
- an unreadable or identity-mismatched destination;
- settings changed before publication; or
- staging, flush, validation, or promotion failure.

Before cached bytes are released, the final-cache check reruns structural and
association validation and records the capture identity when known. Missing or
stale media snapshots retain prior evidence and wait for a reconciling Ready or
lifecycle stage. Unmatched evidence stays bounded in RAM; true ambiguity remains a
failure, and removing another candidate later cannot manufacture certainty.

## Bounded recovery

- Complete captures can wait in RAM for their matching completed listen.
- A delayed final decoder callback may complete an already retained candidate.
- Incomplete owned MP3 files remain recognizable and are repaired at their original
  path after a later complete episode capture.
- `.previous` files make interrupted upgrades recoverable.
- Publication-queue pressure retains a complete candidate for retry rather than
  dropping it immediately.
- Capacity eviction and shutdown run the same final validation/reporting path.

Recovery never means publishing corrupt bytes or overwriting an unrelated file.

## Diagnostics

Normal outcomes go to `Floggfy.log`. Optional detailed ownership, media, hook, and
network observations go to `Floggfy-diagnostics.log`; both are size bounded. Logs
use sanitized facts and state transitions rather than credentials, request bodies,
or arbitrary client exceptions. See [`async_log.cpp`](../../native/async_log.cpp)
and diagnostic handling in [`metadata_bridge.cpp`](../../native/metadata_bridge.cpp).

For a failed save, correlate these stages in order:

1. capture begin and codec;
2. first decoded output and owner;
3. playback/listen generation binding;
4. capture completion and whole-buffer validation;
5. completed listen;
6. association decision; and
7. publication or explicit retention/skip reason.

That ordering distinguishes capture corruption, missing playback evidence,
ambiguity, incomplete listening, and filesystem refusal without guessing from one
final error line.

