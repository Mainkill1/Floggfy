#pragma once
#include "telemetry_policy.h"
#include "../tooling/history_settings.h"

namespace privacy {
struct TelemetryOption {
    const wchar_t* key;
    std::wstring_view renderer_key;
    bool history::Settings::*member;
    Rule rule;
    bool default_value=true;

    constexpr bool IsCategory() const noexcept { return rule != Rule::Allow; }
};

// Master and dump are controls. Only the nine categories have a rule bit and
// a renderer counter (the renderer reports their names without "block_").
inline constexpr std::array<TelemetryOption,11> kTelemetryOptions{{
    {L"BlockTelemetry",       L"block_telemetry",        &history::Settings::block_telemetry,        Rule::Allow, false},
    {L"BlockEventReports",    L"block_event_reports",    &history::Settings::block_event_reports,    Rule::Gabo},
    {L"BlockPlaybackReports", L"block_playback_reports", &history::Settings::block_playback_reports, Rule::Playback},
    {L"BlockGrowthAnalytics", L"block_growth_analytics", &history::Settings::block_growth_analytics, Rule::Growth},
    {L"BlockSentry",          L"block_sentry",           &history::Settings::block_sentry,           Rule::Sentry},
    {L"BlockCrashReports",    L"block_crash_reports",    &history::Settings::block_crash_reports,    Rule::Crash},
    {L"BlockInternalLogging", L"block_internal_logging", &history::Settings::block_internal_logging, Rule::Logging},
    {L"BlockTrackErrors",     L"block_track_errors",     &history::Settings::block_track_errors,     Rule::TrackError},
    {L"BlockAITelemetry",     L"block_ai_telemetry",     &history::Settings::block_ai_telemetry,     Rule::AITelemetry},
    {L"BlockQueuedTelemetry", L"block_queued_telemetry", &history::Settings::block_queued_telemetry, Rule::QueuedTracking},
    {L"DumpTelemetry",        L"dump_telemetry",         &history::Settings::dump_telemetry,         Rule::Allow, false}
}};

// A session override only: callers keep the persisted Settings unchanged.
inline history::Settings EffectiveOptions(history::Settings options) {
    if(options.network_kill_switch_telemetry_lock&&options.network_kill_switch==1) {
        options.block_telemetry=true;
        for(const auto& category:kTelemetryOptions)
            if(category.IsCategory())options.*category.member=true;
    }
    return options;
}
}
