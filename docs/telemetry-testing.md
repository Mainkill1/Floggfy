# Local telemetry suppression testing

For issue #28. This module is experimental and opt-in. Set `BlockTelemetry=1`
under `[To Disk]`, then restart Spotify. `0` (default) installs no privacy hooks.
Recording and metadata preferences do not control it. Use `LogLevel=4` to see
fixed capability fields and aggregate block counters; ordinary diagnostics never
include request URLs or bodies. The default log level reports partial/unsupported coverage warnings.

Current policy blocks only these known routes:

| Destination | Route |
|---|---|
| spclient.wg.spotify.com | /gabo-receiver-service and children |
| sp://logging | /v3 and children |
| o22381.ingest.sentry.io | /api/114855 and children |

The Gabo route is identified in [spotify-adblock's policy](https://github.com/abba23/spotify-adblock/blob/main/config.toml).
The Sentry host/project and EventSender/UBI objects were verified in the installed
Spotify 1.3.9.2055 V8 snapshot. [Spicetify's producer patches](https://github.com/spicetify/cli/blob/main/src/preprocess/preprocess.go)
are a reference for preserving return contracts; Floggfy does not modify installed
Spotify files or change endpoints into nonexistent paths.

Native filtering handles standalone `cef_urlrequest_create` and observed browser
resource callbacks before forwarding. These are separate entry points. Known
EventSender storage/transmission routines and its timer are suppressed through
existing bounded object discovery. UI interactions keep their expected null-ID
objects; flush operations keep resolved Promise results. Impression methods keep
their original IDs and local callbacks so visibility polling can settle, while
the shared sender suppresses queuing. Navigation state updates remain intact. Metadata, lyrics, login, playback, ads and updates are not targets.

## Coverage limits

The startup monitor and load-start bootstrap cannot guarantee execution before
Spotify's V8 snapshot initializes its logging services. Producer discovery is
late (`early=0`), and already-running uploads cannot be cancelled by patching an
instance afterward. Unobserved callback implementations, null resource handlers
on redirects, service workers and other native networking stacks may bypass the
covered callbacks. Unknown URLs/ABIs pass through. Overall status stays partial;
installed hooks or an enabled INI value do not prove complete privacy protection.
No proxy, firewall enforcement or interception certificate is installed.

## Checks

`bash test-telemetry.sh` runs URL-policy, producer, Windows/MinHook cancellation,
reference ownership and settings tests in isolated temporary directories. It
checks that known blocked requests never call the original forwarding function,
while functional routes preserve arguments, references and LastError. These
checks do not establish absence of traffic from the installed Spotify process.

For live testing, retain a rollback copy of the 1.3 DLL and INI. With privacy
opted in, check login, normal playback, a complete recording, lyrics and metadata,
then inspect capability and rule counters. Also test with Downloads=0 and
Metadata=0. Independently capture process/network activity during controlled
telemetry requests to check egress; a returned request error alone is insufficient.
HTTPS packets alone cannot distinguish paths on a shared Spotify connection.
Keep this branch local until those checks establish the actual supported paths.

## Inspecting blocked requests

Set `DumpTelemetry=1` together with `BlockTelemetry=1`, then restart. The optional
`Floggfy-telemetry.log` is written in Save Location. Each entry places its UTC time,
method, destination, blocking rule, interception point, content type/encoding and
body lengths above a readable payload. JSON is formatted for inspection. Binary,
encoded, invalid or incomplete JSON is labeled explicitly; it is not guessed to
be decoded application data.

This captures future blocked application requests, not network packets or old
counter entries. It does not capture allowed requests or events suppressed before
request creation. URL userinfo/query/fragment and authorization/cookie headers are
omitted; the captured body itself is retained for inspection and can contain
identifiers or other account data. Payload contents stay in this separate local
file and are not copied into normal diagnostics.

Capture is limited to 256 KiB per request, eight queued requests and 32 captured
requests per process. The log stops accepting entries at 5 MiB; move or remove it
to start a new investigation. More than 64 body elements are omitted rather than
materialized. Entries and status reports identify truncation, unsupported/file-backed
data and omissions. Logging failure never changes the
blocking decision. Copying happens before releasing CEF request objects; writing
and formatting happen outside request callbacks. Disable `DumpTelemetry` and
restart after the investigation.

The first installed-client samples were `application/x-protobuf` with gzip
encoding, despite the separate JavaScript uploader supporting JSON. Native dumps
preserve these complete bytes as labeled base64. Human-readable protobuf analysis
requires offline decompression and wire decoding; field numbers are retained where
no schema is known. Do not treat the base64 fallback as decoded JSON.
