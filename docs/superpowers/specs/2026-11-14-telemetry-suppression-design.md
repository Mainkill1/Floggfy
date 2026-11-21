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

## Authorized follow-up: readable request inspection

The user requested payload inspection to assess false positives and specified a
single human-readable log, with time/destination/details above formatted JSON.
Add restart-only `DumpTelemetry=0`; capture bounded copies of future cancelled
requests and drain them outside callbacks into Save Location/Floggfy-telemetry.log.
This explicit diagnostic option is the exception to the original no-body-logging
scope. Default diagnostics remain counters only. Omit credentials in headers and
URL query/userinfo, preserve raw body meaning, label incomplete/encoded data and
never let diagnostics change cancellation or allowed forwarding. No proxy, packet-capture service or extra log viewer is needed.

The user subsequently required decoded information directly in this same log.
Actual samples prove gzip-compressed protobuf is needed. Add bounded worker-only
gzip validation/decompression using vendored inflate-only puff and generic wire
field JSON rendering. Preserve unknown field numbers and explicit inference;
no schema guesses, live decoding service or extra runtime DLL. Tests must replay
the exact captured format plus corruption/expansion limits before installation.

The user requested removal of redundant base64, compact request headers, and
schema-aware construction of payload data. Most base64 volume duplicated already
decoded nested messages; retain base64 only for opaque values. Normal request
headers omit false/zero diagnostics, with warnings preserved for incomplete input.
Offline extraction of the installed client's FileDescriptorProto definitions
provides verified envelope/event/context field names and types. Render recognized
Gabo messages with those names and repeated arrays; retain unknown wire fields
without guessing or dropping duplicate fragment names. No runtime binary scanning
or protobuf runtime dependency is added.
