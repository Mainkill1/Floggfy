#pragma once
// Generated factual protobuf field metadata; no payloads or Spotify code.
// Source/version/offsets: docs/telemetry-schema-source.md.
namespace privacy::schema {
enum Type : unsigned {
    TYPE_DOUBLE=1, TYPE_FLOAT=2, TYPE_INT64=3, TYPE_UINT64=4, TYPE_INT32=5,
    TYPE_FIXED64=6, TYPE_FIXED32=7, TYPE_BOOL=8, TYPE_STRING=9, TYPE_GROUP=10,
    TYPE_MESSAGE=11, TYPE_BYTES=12, TYPE_UINT32=13, TYPE_ENUM=14,
    TYPE_SFIXED32=15, TYPE_SFIXED64=16, TYPE_SINT32=17, TYPE_SINT64=18
};
struct Field { const char* message; const char* name; unsigned number; unsigned type; bool repeated; const char* nested_message; };
struct MessageAlias { const char* event_type; const char* message; };
inline constexpr const char* request_message="spotify.event_sender.proto.PublishEventsRequest";
inline constexpr const char* payload_fragment="message";
inline constexpr Field fields[] = {
    {"spotify.event_sender.proto.EventEnvelope", "event_name", 2, 9, false, ""},
    {"spotify.event_sender.proto.EventEnvelope", "event_fragment", 3, 11, true, "spotify.event_sender.proto.EventEnvelope.EventFragment"},
    {"spotify.event_sender.proto.EventEnvelope", "sequence_id", 4, 12, false, ""},
    {"spotify.event_sender.proto.EventEnvelope", "sequence_number", 5, 3, false, ""},
    {"spotify.event_sender.proto.EventEnvelope.EventFragment", "name", 1, 9, false, ""},
    {"spotify.event_sender.proto.EventEnvelope.EventFragment", "data", 2, 12, false, ""},
    {"spotify.event_sender.proto.PublishEventsRequest", "event", 1, 11, true, "spotify.event_sender.proto.EventEnvelope"},
    {"spotify.event_sender.proto.PublishEventsRequest", "suppress_persist", 2, 8, false, ""},
    {"spotify.event_sender.proto.ApplicationDesktop", "version_string", 1, 9, false, ""},
    {"spotify.event_sender.proto.ApplicationDesktop", "version_code", 2, 3, false, ""},
    {"spotify.event_sender.proto.ApplicationDesktop", "session_id", 3, 12, false, ""},
    {"spotify.event_sender.proto.DeviceDesktop", "platform_type", 1, 9, false, ""},
    {"spotify.event_sender.proto.DeviceDesktop", "device_manufacturer", 2, 9, false, ""},
    {"spotify.event_sender.proto.DeviceDesktop", "device_model", 3, 9, false, ""},
    {"spotify.event_sender.proto.DeviceDesktop", "device_id", 4, 9, false, ""},
    {"spotify.event_sender.proto.DeviceDesktop", "os_version", 5, 9, false, ""},
    {"spotify.event_sender.proto.ClientContextId", "value", 1, 9, false, ""},
    {"spotify.event_sender.proto.ClientId", "value", 1, 12, false, ""},
    {"spotify.event_sender.proto.InstallationId", "value", 1, 12, false, ""},
    {"spotify.event_sender.proto.MonotonicClock", "id", 1, 3, false, ""},
    {"spotify.event_sender.proto.MonotonicClock", "value", 2, 3, false, ""},
    {"spotify.event_sender.proto.Sdk", "version_name", 1, 9, false, ""},
    {"spotify.event_sender.proto.Sdk", "type", 2, 9, false, ""},
    {"spotify.event_sender.proto.Time", "value", 1, 3, false, ""},
    {"spotify.event_sender.proto.EventSenderInternalError2NonAuth", "error_types", 1, 9, true, ""},
    {"spotify.event_sender.proto.EventSenderInternalError2NonAuth", "error_total_counts", 2, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderInternalError2NonAuth", "error_unreported_counts", 3, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "sequence_ids", 1, 12, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "event_names", 2, 9, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "loss_stats_num_entries_per_sequence_id", 3, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "loss_stats_event_name_index", 4, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "loss_stats_storage_sizes", 5, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "loss_stats_sequence_number_mins", 6, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "loss_stats_sequence_number_nexts", 7, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "ratelimiter_stats_event_name_index", 8, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "ratelimiter_stats_drop_count", 9, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "drop_list_num_entries_per_sequence_id", 10, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "drop_list_event_name_index", 11, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "drop_list_counts_total", 12, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "drop_list_counts_unreported", 13, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_event_name_index", 15, 5, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_entered_counts_total", 16, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_rejected_client_counts_total", 17, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_dropped_counts_total", 18, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_persisted_counts_total", 19, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_rejected_backend_counts_total", 20, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_delivered_counts_total", 21, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderStats2NonAuth", "states_list_deleted_counts_total", 22, 3, true, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "batch_uuid", 1, 9, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "event_id", 2, 3, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "batch_size", 3, 3, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "sending_system", 4, 9, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "sent_as_pending_event", 5, 8, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "batch_start_time_ms", 6, 3, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "event_time_ms", 7, 3, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "configuration", 8, 9, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "experiment_variant", 9, 9, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "mutation_count", 10, 3, false, ""},
    {"spotify.event_sender.proto.EventSenderCanaryNonAuth", "expected_mutation_count", 11, 3, false, ""},
};
inline constexpr MessageAlias event_aliases[] = {
    {"EventSenderInternalError2NonAuth", "spotify.event_sender.proto.EventSenderInternalError2NonAuth"},
    {"EventSenderStats2NonAuth", "spotify.event_sender.proto.EventSenderStats2NonAuth"},
    {"EventSenderCanaryNonAuth", "spotify.event_sender.proto.EventSenderCanaryNonAuth"},
};
inline constexpr MessageAlias context_aliases[] = {
    {"context_application_desktop", "spotify.event_sender.proto.ApplicationDesktop"},
    {"context_device_desktop", "spotify.event_sender.proto.DeviceDesktop"},
    {"context_client_context_id", "spotify.event_sender.proto.ClientContextId"},
    {"context_client_id", "spotify.event_sender.proto.ClientId"},
    {"context_installation_id", "spotify.event_sender.proto.InstallationId"},
    {"context_monotonic_clock", "spotify.event_sender.proto.MonotonicClock"},
    {"context_sdk", "spotify.event_sender.proto.Sdk"},
    {"context_time", "spotify.event_sender.proto.Time"},
};
} // namespace privacy::schema
