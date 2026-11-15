# Local telemetry suppression testing

For issue #28. This module is experimental and opt-in. Set `BlockTelemetry=1`
under `[Telemetry]`, then restart Spotify. `0` (default) installs no privacy hooks.
Recording and metadata preferences do not control it. Use `LogLevel=4` to see
fixed capability fields and aggregate block counters; ordinary diagnostics never
include request URLs or bodies. The default log level reports partial/unsupported coverage warnings.

The master controls these independent categories. Set any category to `0` to
allow it again. Category defaults are `1`; the master and dump defaults are `0`.
RC1 settings under `[To Disk]` and interim `[Blocking]` values migrate to
`[Telemetry]`; explicit values in the new section take precedence. Migration preserves custom settings.

| Option | Reporting category |
|---|---|
| `BlockEventReports` | Gabo event batches |
| `BlockPlaybackReports` | Melody playback starts, statistics and media event reports |
| `BlockGrowthAnalytics` | Website growth analytics beacons |
| `BlockSentry` | Known Sentry error-reporting project |
| `BlockCrashReports` | Crash upload destination and recognized crash-reporter launches |
| `BlockInternalLogging` | Cosmos logging v1, v2 and v3 |
| `BlockTrackErrors` | Track-error reports |
| `BlockAITelemetry` | Known AI sandbox/turn-termination telemetry paths |

URL policy uses these narrow destinations:

| Destination | Route |
|---|---|
| spclient.wg.spotify.com and regional `*-spclient.spotify.com` hosts | /gabo-receiver-service and children |
| Same client hosts | /melody/v1/msg and /melody/unauth/v1/msg reporting routes |
| Same client hosts | /track-error/v1/errors |
| Same client hosts | /v1/telemetry/sandbox and /v1/telemetry/turn-termination |
| www.spotify.com | /api/growth-events and children |
| crashdump.spotify.com | Crash uploads |
| sp://logging | /v1, /v2 and /v3 and children |
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
Renderer request guards cover observed fetch, XHR, beacon and Cosmos entry
points; they report attachment separately from actual block counts. Guards are
installed after renderer initialization, so earlier activity remains unverified.
The crash control removes the upload URL only from a recognized
`crashpad_handler.exe` launch in the Spotify install directory, preserving the
local database and IPC arguments. Crashpad documents that omitting `--url`
[disables uploads](https://github.com/chromium/crashpad/blob/main/handler/crashpad_handler.md).
The native crash control applies only to recognized future reporter launches;
existing reporters and launches before attachment are not protected. Unknown
Hermes/native transports and dynamically selected service destinations can bypass
these controls. AI path rules are based on installed-client strings; their active
host/call path has not been established. No proxy, firewall enforcement or
interception certificate is installed.

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
method and destination, blocking rule/hook, content type/encoding and captured/decoded
byte sizes above the payload. Normal headers are compact; incomplete capture and
decoding problems produce explicit warnings and relevant part details. JSON is formatted for inspection. Gzip
bodies are decompressed and checked before formatting. Protobuf is rendered as
JSON using [verified Spotify message definitions](telemetry-schema-source.md) for
recognized Gabo envelopes, observed event types and context fragments. Repeated
fields are arrays. Unknown layouts retain numbered wire fields and explicitly
inferred nested messages; their names and units are not guessed. Successful
nested decoding is shown once, without a duplicate base64 copy. Opaque binary
fields retain their bytes with an explicit label. Unsupported, invalid or
incomplete data retains a labeled raw representation and a decoding reason.

This captures future blocked application requests, not network packets or old
counter entries. It does not capture allowed requests or events suppressed before
request creation. Renderer cancellations use the same log with `renderer-fetch`, `renderer-xhr`,
`renderer-beacon` or `renderer-cosmos` hook labels, preserving their block-time
UTC timestamp. Capture remains optional and never changes cancellation. String
and in-memory byte bodies are bounded; Blobs are sliced before reading. Request
streams, file bodies, FormData and unsupported values are omitted and marked
incomplete, rather than consumed or serialized speculatively. Crash-launch suppression is reported
separately and does not represent a captured network request. URL userinfo/query/fragment and authorization/cookie headers are
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
encoding, despite the separate JavaScript uploader supporting JSON. The log writer
now decodes that combination directly in Floggfy-telemetry.log. Decoding is bounded
to 1 MiB expanded data, protobuf depth eight, 4,096 fields and 512 KiB JSON output.
Invalid checksums, malformed protobuf or exceeded bounds produce an explicit
fallback reason instead of an apparently complete decoded payload.
