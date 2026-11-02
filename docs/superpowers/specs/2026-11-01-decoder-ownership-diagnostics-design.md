# Decoder Ownership Diagnostics Design

**Status:** Approved direction; diagnostics must prove the remaining runtime invariants before publication uses decoder ownership.

**Scope:** This document defines the complete ownership model and the first, evidence-only implementation. The diagnostic phase must not change listen eligibility, capture retention, metadata assignment, publication, filenames, or capture acceleration.

## Problem

The current publisher independently observes compressed decoder activity and Spotify playback metadata, then joins them with capture birth times, completion times, durations, and retention windows. That makes a preloaded next track indistinguishable from the active track until more evidence arrives. At accelerated capture speeds the complete current capture, an incomplete next capture, and delayed app metadata routinely coexist. The timing candidate graph can therefore reject a valid capture, manufacture uniqueness after an alternative expires, or mishandle a same-song repeat.

Decoder ownership replaces the many-to-many timing join with two explicit links:

```text
compressed capture session --exact decoder owner--> decoded activity
decoded activity --Spotify playback instance--> metadata/listen generation
```

The first link can be proven from the supported Spotify binary layout. The second must be demonstrated at runtime with Spotify's `playbackId`; URI, title, duration, and temporal proximity are not ownership evidence.

## Trust hierarchy

From strongest to weakest:

1. A structurally verified relationship between a codec hook context and the shared decoded-output context.
2. A decoded-output observation from that exact normalized context.
3. A Spotify PlayerAPI `playbackId` transition with a bounded, validated renderer timestamp.
4. Complete listen coverage for that exact `playbackId`.
5. Independently reparsed, complete compressed bytes.
6. Duration and ordinary timestamps, which are sanity checks only.

Automatic publication must eventually require items 1 through 5. Item 6 may reject a binding but must never select one. If an observation is missing, contradictory, or lost, the system fails closed: keep the bounded complete capture for final audit, but do not assign metadata or publish it.

## Current supported-client evidence

These are RVAs from the Spotify DLL installed on the WSL host on 2026-11-01. They are diagnostic facts, not constants to copy into production discovery.

| Target | RVA | Relevant fact |
|---|---:|---|
| Shared decoded dispatcher | `0x00d90d08` | Calls virtual slot `+0x08`, then advances decoded floats and encoded bytes by returned counts. |
| Playback pump | `0x00d91948` | Loads decoder from pump `+0x70`, calls the dispatcher at `0x00d91c11`, and reaches stereo accounting at `0x00d91dd4`. |
| Ogg page seek | `0x00df0fc8` | Existing Ogg capture hook. |
| Ogg page wrapper | `0x00df0f78` | Passes its `RCX` unchanged to the page function. |
| Ogg parser method | `0x00dcfc64` | Calls the page wrapper with `parser + 0x70`. |
| Ogg decoder method | `0x00dd0334` | Calls the parser with `decoder + 0x18`. |
| Ogg decoder vtable | `0x01b04918` | Virtual slot `+0x08` is the decoder method. |
| FLAC init | `0x00dcadf8` | Existing init hook; `RCX` is the decoder object. |
| FLAC decode method | `0x00dcb00c` | Virtual slot `+0x08` called by the shared dispatcher. |
| FLAC read/frame/error | `0x00dcb508` / `0x00dcb8c0` / `0x00dca880` | Callback thunks restore the init client-data pointer as `RCX`. |
| FLAC decoder vtable | `0x01b039e8` | Contains init and the slot `+0x08` decode method. |
| AAC init/push/destroy | `0x00dd7bc4` / `0x00dd8758` / `0x00dd7da0` | Existing AAC hooks. |
| AAC compressed-reader vtable | `0x01b04f20` | `+0x00` destroy, `+0x08` leaf accessor at `0x00ad6278`, `+0x10` compressed push. It is not the decoded-dispatch object. |

The supported layout therefore normalizes capture contexts as follows:

```text
FLAC decoder owner = FLAC capture context
Ogg  decoder owner = Ogg page context - (0x18 + 0x70)
                   = Ogg page context - 0x88
AAC  decoder owner = unproven; diagnostics must report unavailable
```

The `-0x88` value must be extracted from validated instructions. It must not be accepted merely because it equals the value above.

## Static ownership discovery

Extend `native/spotify_hook_discovery.h` with:

```cpp
struct DecoderOwnershipLayout {
    std::uint32_t decoded_output = 0;
    std::int32_t ogg_capture_to_decoder = 0;
    std::int32_t flac_capture_to_decoder = 0;
    std::int32_t aac_capture_to_decoder = 0;
    bool aac_verified = false;
};

DiscoveryResult DiscoverDecoderOwnershipLayout(
    const std::uint8_t* mapped_image,
    std::size_t available,
    DecoderOwnershipLayout& layout) noexcept;
```

`DiscoverDecodedOutputTarget` remains available for compatibility and delegates to, or shares validators with, the stronger discovery.

Discovery succeeds only if it proves all enabled relationships:

### Shared decoded dispatcher and pump

- Retain the existing dispatcher prefix, virtual-call, decoded-span, and encoded-span checks.
- Retain the unique playback-pump function and direct-call validation.
- Validate the pump instruction that loads the dispatcher `RCX` from pump object `+0x70`.
- Retain the stereo float-count division and output accounting anchor.

### Ogg

- Locate the unique Ogg outer decoder virtual method through its vtable slot `+0x08`.
- Validate that the outer method makes a direct call to the parser method and supplies `RCX` using a bounded positive `lea [decoder+offset]` displacement.
- Validate that the parser method directly calls the existing Ogg page wrapper and supplies `RCX` using another bounded positive `lea [parser+offset]` displacement.
- Validate that the wrapper passes `RCX` unchanged to the already discovered page function.
- Add the two displacements with checked signed arithmetic and return their negation as `ogg_capture_to_decoder`.
- Reject zero, negative source displacements, overflow, displacements greater than `0x1000`, non-unique call chains, or any target outside the containing runtime function.

### FLAC

- Locate one unique read-only vtable containing the discovered FLAC init and an executable slot `+0x08` decode method.
- Validate that the shared dispatcher ABI matches that decode slot.
- Validate the init instructions that retain `RCX` as the decoder object and pass that same object as the libFLAC client-data argument.
- Validate each read/frame/error callback thunk that moves the callback client-data register into `RCX` before reaching the existing hook target.
- Return `flac_capture_to_decoder = 0` only after the full chain passes.

### AAC

- Reuse the existing constructor/vtable validation only to prove the compressed-reader object.
- Confirm that its slot `+0x08` is not treated as the shared decoder merely because it is executable; on the current client it is a leaf accessor with no unwind entry.
- Return `aac_verified = false`. A future nonzero/zero AAC context delta requires a separately proven object relationship and its own tests.
- Missing AAC ownership evidence never invalidates proven FLAC/Ogg ownership and never changes existing AAC capture behavior during diagnostics.

The current-client discovery test must print the discovered offsets and RVAs. Synthetic tests must mutate every connecting instruction, displacement, vtable entry, and direct call independently and expect `InconsistentLayout` or `AmbiguousTarget`.

## Safe context normalization

Create `native/decoder_ownership.h` and `native/decoder_ownership.cpp`. Pointer arithmetic is centralized in:

```cpp
bool NormalizeDecoderOwner(std::uintptr_t capture_context,
                           std::int32_t capture_to_decoder,
                           std::uintptr_t& owner) noexcept;
```

It rejects a null input, signed underflow/overflow, a null result, and a result not aligned to `alignof(void*)`. Hook callbacks never perform the normalization; the worker does so using the immutable discovered layout.

## Runtime evidence model

All mutable ownership state belongs to the existing audio worker thread. Hooks only emit bounded POD observations.

### Identifiers

- `DecoderOwner`: normalized address used only inside the process.
- `OwnerToken`: monotonically allocated diagnostic ID; raw addresses are never logged.
- `CaptureToken`: monotonically allocated for every FLAC begin, AAC begin, or Ogg BOS/serial.
- `PlaybackToken`: monotonically allocated per distinct non-empty Spotify `playbackId`; raw playback IDs are never logged.
- `listen_generation`: existing `Listen::generation`, whose `ListenKey()` already contains `playback_instance`.

Pointer reuse is not session identity. A new codec begin/BOS always receives a new `CaptureToken`, even when its normalized owner is unchanged.

### Public tracker interface

`native/decoder_ownership.h` exposes portable, Windows-independent types:

```cpp
enum class DecoderCodec : std::uint8_t { Ogg, Flac, Aac };
enum class OwnershipState : std::uint8_t {
    AwaitingOutput,
    AwaitingPlaybackInstance,
    Bound,
    Conflict,
    Invalidated,
};
enum class CaptureAbortReason : std::uint8_t {
    DecoderError,
    InvalidData,
    Reinitialized,
    MemoryLimit,
    QueueLoss,
    SettingsChange,
    Timeout,
    Shutdown,
};
enum class ObservationLoss : std::uint8_t {
    DecodedQueue,
    CaptureQueue,
    Media,
    Epoch,
};

struct DecodedActivity {
    std::uintptr_t owner = 0;
    std::uint64_t calls = 0;
    std::uint64_t produced_floats = 0;
    std::uint64_t consumed_bytes = 0;
    std::uint64_t kept_floats = 0;
    double first = 0;
    double last = 0;
    unsigned epoch = 0;
};

struct OwnershipBinding {
    std::uint64_t capture_token = 0;
    std::uint64_t playback_token = 0;
    std::uint64_t listen_generation = 0;
    OwnershipState state = OwnershipState::AwaitingOutput;
};

class DecoderOwnershipTracker {
public:
    std::uint64_t BeginCapture(DecoderCodec codec, std::uintptr_t owner,
                               std::uint32_t stream, double time, unsigned epoch);
    void CompleteCapture(std::uint64_t capture_token, double time);
    void AbortCapture(std::uint64_t capture_token, CaptureAbortReason reason);
    void ObserveDecoded(const DecodedActivity& activity);
    void ObservePlayback(const std::string& playback_id, const std::string& uri,
                         std::uint64_t listen_generation, double observed);
    void ObserveTransition(const std::string& playback_id, const std::string& uri,
                           double transition_time, bool explicit_skip,
                           bool natural_transition);
    void ObservationLost(ObservationLoss source, double time);
    void Reset(unsigned epoch);
    OwnershipBinding Binding(std::uint64_t capture_token) const;
};
```

Implementation storage is bounded to the same practical windows as capture: at most 32 capture records, 16 decoder owners, and 16 playback instances. Eviction invalidates dependent records; it never creates a unique binding.

### Diagnostic state rules

The first phase computes and logs states but does not expose them to `SchedulePublications`.

- `BeginCapture` creates `AwaitingOutput` and associates the capture with one normalized owner.
- `ObserveDecoded` marks every live capture on that owner as having output evidence. More than one viable live capture for the owner is `Conflict`, except that an Ogg BOS can supersede a completed serial only after no later output was attributed to the old serial.
- A new output owner while another owner is current becomes a pending successor. It is never assigned the current playback instance.
- A pending successor may bind only after a distinct non-empty `playbackId` is observed or transitioned.
- The current owner may bind to the current non-empty `playbackId` only if the playback observation is not older than the first decoded observation and there is no pending transition or observation-loss marker.
- Same URI plus different `playbackId` is a new playback instance.
- Same URI plus missing or unchanged `playbackId` does not prove a repeat and remains unresolved.
- More than one pending successor, output from an aborted capture, context-normalization failure, queue overflow, media observation loss, or epoch change invalidates the affected evidence.
- The ten-second value is an information-retention deadline. It never chooses a candidate. Expiry produces an unresolved diagnostic outcome.

These rules intentionally reject evidence rather than guess. Live diagnostics determine whether Spotify's actual ordering is sufficient. Production binding is a later plan and may tighten these rules.

## Decoded hook observation

The existing hook signature remains:

```cpp
using DecodedProcess = void*(*)(void*, void*, decoded_output::Span*,
                                decoded_output::Span*, bool);
```

`DecodedSinkHook` must:

1. Snapshot decoded and encoded spans.
2. Call the original dispatcher.
3. Preserve `GetLastError()`.
4. Validate both advanced spans at every capture speed, not only when speed is greater than one.
5. Compute `produced_floats` and `consumed_bytes` before acceleration changes the returned decoded span.
6. Apply `Accelerator::Keep` exactly as today and compute `kept_floats`.
7. Queue a small decoded-activity observation after the original call and before returning.
8. Restore `GetLastError()`.

An invalid span disables acceleration as today and separately marks ownership observation lost. It must not synthesize zero activity.

### Small activity queue

Do not place decoded observations in the existing `Slot`, which contains a 65,307-byte payload and is copied by `Pop`. Add a dedicated fixed ring in `audio_history.cpp`:

```cpp
struct DecodedActivitySlot {
    std::uintptr_t context = 0;
    std::uint64_t calls = 0;
    std::uint64_t produced_floats = 0;
    std::uint64_t consumed_bytes = 0;
    std::uint64_t kept_floats = 0;
    double first = 0;
    double last = 0;
    unsigned epoch = 0;
};
```

- Capacity: 256.
- Use a dedicated `SRWLOCK`; never allocate or format in the hook.
- Consecutive observations for the same context and epoch coalesce with checked, saturating counters.
- On overflow, increment a dedicated atomic loss counter and wake the worker.
- The worker drains this ring before reading media snapshots on each iteration.
- Any changed loss counter calls `DecoderOwnershipTracker::ObservationLost(ObservationLoss::DecodedQueue, now)`.

Exact cross-queue ordering is represented by monotonic timestamps. Equal timestamps or regressions are conflicts, not reordered assumptions.

## Capture integration

Add to `Capture`:

```cpp
std::uintptr_t decoder_owner = 0;
std::uint64_t capture_token = 0;
```

At FLAC begin, AAC begin, and Ogg BOS:

1. Normalize the raw hook context with the codec's discovered delta.
2. If normalization fails, keep capturing bytes but mark the capture ownership-invalid.
3. Create the `Capture` and call `BeginCapture`.

On complete, call `CompleteCapture`. Every path that removes an active capture calls `AbortCapture` before destruction. `CheckBeforeRelease` remains the final byte audit and must include the ownership state in its diagnostic reason without changing the current release behavior.

`AudioEventKind` currently relies on ordinal comparisons in `ProcessCaptureEvent`. Do not insert a decoded event into that enum. The small queue is separate. When production ownership work later changes dispatch, replace ordinal comparisons with an explicit `switch` first.

## Playback-instance observations

The collector already obtains PlayerAPI `playbackId`, emits it as `instance`, and requires a changed non-empty instance for positive natural-transition evidence. No new JavaScript hook is required for the diagnostic phase.

In `Worker`:

- After a `MediaRead::Ready`, call `ObservePlayback` with `media.playback_instance`, `media.spotify_uri`, the post-`AssociateListen` generation, and `observed_at`.
- For `MediaRead::Transition`, call `ObserveTransition` before `FinishAppTransition`, using the validated transition time.
- For stale, failed, overflowed, or unavailable media observations, call `ObservationLost(ObservationLoss::Media, now)` once per loss episode.
- Empty playback instances are recorded as unavailable evidence and never tokenized.

The existing collector protocol validation, bounded UTC-to-monotonic conversion, listen coverage, and natural/manual transition logic remain unchanged in the diagnostic phase.

## Diagnostic logging

Logging is enabled only when `Log=1` and `DebugLog=1`. It goes to `Floggfy-diagnostics.log`, not the normal activity log.

No raw pointer, `playbackId`, device ID, or unbounded payload is logged. URI may be represented by the existing stable playback identity or omitted; ownership analysis needs only `PlaybackToken` and whether the URI changed.

Emit only state changes and summaries:

```text
ownership event=layout decoded_rva=... ogg_delta=-136 flac_delta=0 aac_delta=0 aac_verified=1
ownership event=capture-begin capture=12 owner=4 codec=flac stream=0 epoch=7
ownership event=output-first owner=4 calls=1 produced=4096 consumed=1832 kept=410 mono=...
ownership event=output-summary owner=4 calls=287 produced=... consumed=... kept=... first=... last=...
ownership event=playback instance=9 uri_changed=1 listen_generation=14 mono=...
ownership event=transition from=8 to=9 explicit_skip=0 natural=1 mono=...
ownership event=binding capture=12 owner=4 instance=9 listen_generation=14 state=bound
ownership event=conflict capture=... owner=... reason=multiple-successors
ownership event=capture-complete capture=12 owner=4 state=bound bytes=... duration=...
```

Output summaries occur on owner switch, capture completion, playback transition, reset, and at most once per second for a continuously active owner. Logging must never happen in a hook callback.

## Diagnostic acceptance matrix

Use the running Windows client only after all portable and synthetic Windows tests pass. Run each scenario with debug logging enabled and preserve the complete diagnostic interval:

| Scenario | Speeds | Required evidence |
|---|---|---|
| Different-song natural transition | 1x, 10x | One old owner/instance followed by one successor owner/new instance; preload capture does not bind early. |
| Same song twice | 1x, 10x | Two capture tokens and two playback tokens even when URI is identical. |
| Pause/resume | 1x, 10x | Same owner and playback token; no successor. |
| Forward seek and backward seek | 1x | Same owner/token; listen coverage may become ineligible independently. |
| Manual Next and Previous | 1x, 10x | Changed playback token; explicit skip remains visible and cannot complete the prior listen. |
| Crossfade enabled/disabled | 1x | Overlap is measured; multiple unresolved successors fail closed. |
| Gapless/hidden-track boundary | 1x, 10x | Delayed metadata is retained up to ten seconds without assigning old metadata to the successor. |
| FLAC and Ogg | 1x | Normalized owner link exists for both proven codecs. |
| AAC | 1x | Capture remains unchanged and diagnostics explicitly report ownership unavailable. |
| Capture queue or decoded queue loss | synthetic | All affected ownership becomes invalidated; no later removal restores uniqueness. |
| Context reuse | synthetic and live if observed | New capture token; old binding cannot transfer. |

### Pass criteria

- Every complete FLAC/Ogg capture that would be publishable has exactly one normalized owner.
- Every bound owner has decoded activity.
- Every binding uses a distinct non-empty Spotify playback instance.
- Same-song repeats produce distinct playback tokens.
- No preloaded capture binds to the outgoing playback instance.
- No observation-loss interval produces a bound state.
- Hook callback and queue timing remain within existing diagnostic expectations and no new queue overflow occurs.

### Stop criteria

Stop before production integration if any of these occurs:

- decoded activity is produced for multiple speculative successors before playback identity can distinguish them;
- Spotify reuses or omits `playbackId` for a same-song repeat;
- one normalized owner concurrently represents more than one viable capture;
- the decoded dispatcher is not downstream enough to distinguish playback from predecode;
- the structural discovery cannot prove a codec relationship on the current binary.
- AAC is accidentally treated as decoder-owned without a newly proven relationship.

If the dispatcher is too early, the next investigation targets the already validated playback-pump submission path. That fallback needs its own ABI design and tests; this design must not paper over failure with timing matching.

## Production migration after diagnostics

Production ownership is deliberately a separate implementation plan after the acceptance matrix passes. It will:

1. Require a `Bound` ownership record before `SchedulePublications` can associate a capture.
2. Join by `capture_token` and `listen_generation`, not timing or duration.
3. Preserve listen eligibility, seek/pause/manual-skip rejection, final compressed-file validation, memory bounds, publication queue behavior, and final cache audit.
4. Remove `CaptureTimingMatchesListen`, the many-to-many association graph, lost-candidate uniqueness evidence, and provisional accelerated recovery only after replacement tests pass.
5. Disable automatic publication, while retaining bounded capture/final audit, when ownership discovery or runtime evidence is unavailable.

## Non-goals

- Inferring ownership from duration, title, URI, capture start time, or EOS order.
- Treating a complete compressed file as proof it was listened to.
- Changing the user's accepted accelerated-listen semantics: decoded frames count before PCM blocks are dropped.
- Publishing from diagnostics alone.
- Adding a new metadata-construction hook.
- Implementing the playback-pump fallback before the dispatcher evidence is evaluated.
