# Exact capture replay and restart correction

User requested investigation and implementation of tests using the same media buffering path. Preserve current configuration and existing worktree changes.

Evidence: Permagrin Just One More completed recovery but retained a stale metadata ownership claim on aborted capture4; complete capture13 went to Unmatched. dEUS capture33 suffered resync stall and1,001 production event losses. Consumed-byte dump does not reconstruct the exact read-callback buffer; initial overflow cause remains unproven.

- [x] Freeze latest session and validate specific failures against bytes and identity evidence.
- [x] Allow exact metadata claim replacement only through a clean, incomplete same-source FLAC restart chain; test safe replacement and unsafe ownership cases.
- [x] Bound repeated resync work without broadening audio acceptance; test monotonic work, budget exhaustion, large valid frames, and prior real dump recovery.
- [x] Add exact accepted-event journal to DumpStreams with separate capture files and explicit gaps; replay same worker routines using original chunk, callback, reset, and applied-loss order.
- [x] Verify production buffer-to-named-publication integration, journal round trip, full native/Windows suites, then build/install candidate for user validation.

No promise of eliminating all queue overflow: first-trigger timing cannot be established from old consumed-input dumps. New journal captures reader input and applied loss, not complete producer scheduling. Keep512MiB bound,10-second matcher tolerance, and5-start retention.
