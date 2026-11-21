# Telemetry protobuf schema source

The field map in [`telemetry-schema-1.3.9.2055.json`](telemetry-schema-1.3.9.2055.json) and [`telemetry_schema.h`](../native/telemetry/telemetry_schema.h) records protobuf interface metadata extracted offline from the installed Windows `Spotify.dll`. It contains field names, numbers, declared scalar types, repetition labels, and nested message names. It contains no request payloads, account values, executable code, or binary excerpts.

File and product version: `1.3.9.2055` (Windows PE version resource). Size: `44,244,864` bytes. SHA-256: `9991ade34f9986ba60dd1122386f4c6cbdd2a167c85a07ce3c30554b7c38153f`.

Extraction located serialized `FileDescriptorProto` records by their field-1 filename and field-2 package, then parsed complete protobuf wire fields up to the following zero terminator. Message definitions came from descriptor field 4, nested messages from field 3, and message fields from field 2. Descriptor structure and numeric type/label meanings follow the [upstream protobuf descriptor definition](https://github.com/protocolbuffers/protobuf/blob/main/src/google/protobuf/descriptor.proto). No runtime binary scanning or protobuf package is required.

The JSON includes a hash of each descriptor and its exact DLL file offset and serialized length. The native table includes only the request envelope, the three observed event types, and eight requested context types; response schemas are excluded.

| Descriptor filename | File offset (decimal) | Bytes |
| --- | ---: | ---: |
| `gabito.proto` | 28446976 | 669 |
| `context_application_desktop.proto` | 28449008 | 202 |
| `context_device_desktop.proto` | 28449376 | 269 |
| `context_client_context_id.proto` | 28449984 | 114 |
| `context_client_id.proto` | 28450224 | 99 |
| `context_installation_id.proto` | 28450704 | 111 |
| `context_monotonic_clock.proto` | 28450944 | 127 |
| `context_sdk.proto` | 28451424 | 121 |
| `context_time.proto` | 28451664 | 90 |
| `include/EventSenderInternalError2NonAuth.proto` | 30557856 | 252 |
| `include/EventSenderStats2NonAuth.proto` | 30558304 | 1517 |
| `include/EventSenderCanaryNonAuth.proto` | 30560336 | 543 |

`PublishEventsRequest.event` contains `EventEnvelope` entries. Their `event_name` selects the schema for the fragment named `message`; each `event_fragment` has its own `name` and byte-valued `data`. The event aliases exactly match descriptor message names. Context aliases match each `context_*.proto` basename to its sole declared message. These dispatch relationships were checked against the field structure of local validation requests 1 and 2; private values are not reproduced here.

The capture included an empty `context_client_context_id` fragment with omitted `data`, which is a valid empty/default message. Canary fragment bytes that the heuristic renderer labeled as text parsed as field 9 (`experiment_variant`). `Time` field 1 is named `value`, not `timestamp`. `ClientId.value`, `InstallationId.value`, and `EventEnvelope.sequence_id` are bytes, not arbitrary nested messages or text. Numeric repeated fields must accept both packed and unpacked protobuf encodings.

This is a version-specific map, not an exhaustive schema for Spotify traffic. Unknown fields/types must retain their wire representation. Human-facing plural labels such as `events` or `fragments` are presentation aliases; the descriptor field names remain `event` and `event_fragment`.
