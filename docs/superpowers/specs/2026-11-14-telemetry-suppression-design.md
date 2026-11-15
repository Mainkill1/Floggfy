# Local telemetry suppression for issue #28

Implement the user's chosen in-process design on a local branch. No pushes,
release, mandatory proxy, root certificate, firewall changes or on-disk Spotify
patches. Privacy remains independent of recording/enrichment preferences.

## Approach

Use native pre-transmission URL filtering plus suppression of verified runtime
analytics objects. A domain-only block would also break metadata and lyrics on
spclient.wg.spotify.com. A proxy/OS enforcement layer remains a separate follow-up.

Add restart-only `[To Disk] BlockTelemetry=0` (opt-in for testing). Unknown
capabilities/URLs continue normally and report partial or unsupported coverage;
this is not a strict network boundary. Preserve login, playback, advertisements,
updates, artwork, `/metadata/4/` and `/color-lyrics/v2/`.

Native policy parses URLs and matches explicit host/path boundaries. Initial
verified routes: HTTPS spclient.wg.spotify.com/gabo-receiver-service and
sp://logging/v3. Add only other destinations verified in installed app source.
Do not inspect bodies, record full URLs, log credentials or rename endpoints.

Intercept `cef_urlrequest_create` and browser resource requests through the
existing CEF client observation entry. Preserve original CEF objects/callbacks,
check public ABI sizes and function addresses, and honor transferred references
when cancelling locally. Preserve LastError and original results for allowed
requests. Counters use fixed categories, not raw user data. Hook success does not
prove all Spotify traffic is covered; renderer/service-worker/native paths outside
these callbacks remain a reported limitation.

Observe known EventSender and UBI logger objects in the existing bounded registry
walk. Suppress verified event storage/transmission methods, preserving synchronous
objects and resolved-promise contracts; stop known sender timers where safe.
No global Object.prototype, generic send/flush patch, or arbitrary getter calls.
Inject the bootstrap at load start and retry discovery through the existing
collector; the installed V8 snapshot can initialize telemetry earlier, so producer
coverage is explicitly late/partial, never claimed as pre-startup protection.

## Verification

Tests must show that blocked native requests never call the forwarding function,
allowed same-host functional routes do, and callback reference transfers balance.
Exercise exact hostname/path boundaries and deceptive URLs. JS tests verify real
queue effects and return contracts; failed discovery stays partial. Run existing
native/Windows checks and build a local candidate. Live egress and functional
smoke testing remains required before claiming protection for the installed client.
