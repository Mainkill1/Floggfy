# Telemetry Categories Implementation Plan

**Goal:** Extend verified blocking coverage with a master and per-category controls.
**Architecture:** Preserve the current CEF filter and bounded log. Move privacy settings to [Telemetry], migrate legacy To Disk keys, filter exact reporting paths, add renderer request/producer guards and native crash-reporter upload suppression where verified. No proxy or firewall.
**Spec:** User explicitly requested broad available telemetry suppression and independent INI packet categories, with master first; scope follows coverage-audit/findings.txt.
**Defaults:** BlockTelemetry=0 first; BlockEventReports, BlockPlaybackReports, BlockGrowthAnalytics, BlockSentry, BlockCrashReports, BlockInternalLogging, BlockTrackErrors, BlockAITelemetry each 1; DumpTelemetry=0 last. Category switches restore the relevant path when 0. Explicit Blocking keys override legacy keys. Invalid/duplicate opt-ins must not enable blocking. Keep defaults and embedded INI identical.
**Constraints:** No whole shared-domain blocking. Preserve license/login/audio/metadata/lyrics/update paths. No process-wide arbitrary interception, payload spoofing or external configuration changes. Report partial coverage, not guarantees. No RC replacement or release without testing. Existing isolated feature worktree; no main merge.

- [x] Settings and exact URL policy with migration/category regressions.
- [x] Renderer categories: known sender, narrow fetch/XHR/beacon request filtering and Cosmos bridge logging interception with caller contracts preserved; honest capability/count reporting.
- [x] Native crash upload suppression: verify Crashpad no-URL behavior and intercept only its validated launch path; preserve local handler function and other child processes. Report any early launch/other native-stack gaps.
- [x] Integrate category selection/counts into CEF dump pipeline; build, focused tests and independent review.
- [x] Install local candidate with current settings preserved for live validation; record limits and source/binary hashes.

Ruling: Preserve master default-off and default-on categories to give the requested broad protection after explicit opt-in. Do not guess a native protocol ABI for Hermes or unresolved backend hosts. Those remain partial until a reliable interception point exists.

Ruling: User renamed the section to [Telemetry]. Preserve interim [Blocking] values during migration; explicit [Telemetry] keys take precedence.


## Validation

Focused policy, producer/request-contract, CEF cancellation/reference ownership,
per-category off/on, INI migration/precedence and crash argument tests passed.
The runner initially omitted -municode for the restart-options fixture; corrected
that runner option and resumed from that point, with all remaining checks passing.
Real Windows crash-hook fixtures passed enabled, disabled and both rollback modes.
The network-patch startup regression also passed. Independent review found no
remaining blockers after correcting cancellation/readonly-install and XHR Boolean
conversion edge cases. The complete DLL built with only Windows imports.

First installed candidate: Spotify remained responsive on 1.3.9.2055. Existing
master/dump values migrated and all category defaults were enabled. Live diagnostics
recorded newly blocked playback reporting plus Gabo and Sentry requests; fetch/XHR/
beacon guards attached. Cosmos was not attached in this session, and no crashpad
process/launch was observed. Crash upload handling is fixture-verified, not live
upload-verified. No intentional crash, external firewall or released RC change.

The final [Telemetry] rename passed targeted migration tests and a rebuilt
installation. Live INI validation confirmed all enabled master/category/dump
values survived, the old empty [Blocking] header was removed, and every other
setting stayed unchanged. Spotify remained responsive. The new candidate is
local for user testing; published RC1 and main remain unchanged.
