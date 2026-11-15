# Telemetry Suppression Implementation Plan

> Execute locally; user supplied and authorized the in-process design. No push or release.

**Goal:** Implement opt-in pre-transmission telemetry filtering and verified producer suppression for local testing.
**Spec:** ../specs/2026-11-14-telemetry-suppression-design.md
**Architecture:** Small portable URL policy; CEF interception module; separate renderer producer script sharing existing discovery.
**Stack:** C++17, MinHook, CEF public C prefixes, JavaScript, native/Windows tests.

## Constraints and review focus

- Never block entire shared Spotify domains or alter ads/updates.
- Missing ABI/producer support reports partial coverage; no privacy guarantee from an enabled setting.
- No raw URL/body/account logging; bounded counters only.
- Preserve CEF reference transfers, original callbacks and LastError.
- Privacy must work with Downloads=0 and Metadata=0.

## Tasks

- [x] 1. Write failing URL policy tests; implement strict host/path policy and known endpoint inventory.
- [x] 2. Write producer effect/return tests; implement audited EventSender and UBI suppression with bounded existing-registry discovery.
- [x] 3. Write native forwarding/cancellation/lifetime tests; implement standalone and browser CEF request coverage.
- [x] 4. Integrate restart-only INI default, early bootstrap, shared discovery, counters and truthful status; build/test locally.
- [x] 5. Review whole change; record coverage limits and prepare the DLL for controlled live testing. Keep main and remote unchanged.

## Implementation decisions and review

- Audited Spotify 1.3.9.2055 stores application modules in its V8 snapshot.
  Keep producer discovery late and explicitly early=0; no speculative snapshot
  factory rewriting. Independent network enforcement remains a follow-up.
- Review found that null impression IDs prevent UI polling from settling.
  Preserve original impression methods/IDs/local callbacks, suppress the shared
  EventSender sinks, and cover consumer polling completion with a regression.
- Root hooks must be queued together. Privacy Prepare/Finalize joins the existing
  metadata/menu startup batch; tests cover no interception before shared apply
  and retained trampoline state on failed activation.
- Local tests verify no forwarding from covered cancellation paths. Live Spotify
  egress and feature smoke checks remain required; no installed client or network
  configuration is changed by this implementation task.

## Local validation

- Portable native suite, telemetry-focused tests (including five native activation/
  rollback modes), all JavaScript tests, network startup regression and final DLL
  build passed.
- Windows regression suite passed through recording, metadata, filenames, stream
  dumps and logging. Its restart-options fixture then failed because its explicit
  list omitted BlockTelemetry. Updated that fixture and assertions; reran from
  the failed diagnostics component through the remaining suite, all passing.
  No production changes were needed for that fixture correction.
- Optional current-client binary-discovery checks were skipped by the existing
  suite because no Spotify DLL argument was supplied. Live traffic and functional
  validation remain pending; this is a local candidate, not a protection claim.
- Final DLL imports only Windows libraries; no external MinGW runtime dependency.
  Installed Spotify DLL and INI were not changed. Main and remote remain unchanged.

## Follow-up: readable blocked-request log

User requested inspection of cancelled payloads and specified one readable log
with request details above JSON. Implement opt-in DumpTelemetry and format/write
bounded copies outside callbacks; no changes to rules or producer suppression.
Settings defaults/strict opt-in and the full restart-options fixture have passed.
Focused native tests cover JSON/text/binary preservation, off/allowed behavior,
CEF ownership, body/queue/session caps and logging failures. Before installation,
build the DLL and inspect real startup samples; retain prior DLL/INI for rollback.

Follow-up validation: focused telemetry runner, new/strict settings and restart
migration tests passed; final DLL built with Windows-only imports. Installed with
DumpTelemetry=1 and retained prior DLL/INI. Live Gabo requests produced complete
readable headers plus correctly labeled gzip/protobuf bytes. Offline decoding
identified EventSender canary/statistics/internal-error reports with installation
and device contexts. One concurrent capture was omitted (reported busy_dropped=1),
while its blocking decision remained intact. No packet-level egress claim is made.

## Follow-up: decode directly in the log

Implemented worker-side gzip integrity checks and bounded generic protobuf JSON
rendering after the user requested decoded content in the same log. The first real
stats replay exposed 2,075 fields, so the node cap was raised from 2,048 to 4,096
and readable text no longer incurs speculative nested parsing. Five exact private
live samples passed both the portable production helper and Windows FormatDump;
the largest formatted log entry was 413,232 bytes. Synthetic corruption/size/depth/
node/output tests and the full focused telemetry runner passed. Review found no
blocking issues. The final DLL built with Windows-only imports.

Installed the tested DLL, archived the prior encoded-only log, and restarted with
DumpTelemetry=1. Live log validation found two valid decoded gzip/protobuf JSON
entries (canary, sender stats/internal errors) and one readable three-record Sentry
JSON envelope. All three captured requests were written, with zero truncations,
unsupported fields, busy/queue drops or write failures at that observation.
