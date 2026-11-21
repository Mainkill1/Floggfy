# Launcher Child Startup Design

## Intent

Launcher mode must carry Floggfy into Spotify's ordinary network helper. The released launcher loads `Floggfy.dll` into the first `Spotify.exe` before its entry point, but the later network helper currently starts without it.

## Design

Floggfy owns one `CreateProcessW` detour inside each covered process. When an ordinary launch targets `Spotify.exe`, the detour reuses the launcher's validated pre-entry loader. Other executables and caller-suspended Chromium sandbox launches pass through unchanged.

The detour will also retain the existing crash-report URL rewrite. Keeping both behaviors behind one hook prevents competing MinHook detours on `CreateProcessW`.

## Scope

- `HookAllSubProcesses=0` disables child loading, `1` enables it only for launcher-loaded `Floggfy.dll`, and `2` enables it for every load method, including `version.dll`.
- Cover ordinary `Spotify.exe` children created through `CreateProcessW`; do not scan or attach to existing processes.
- Preserve process attributes, thread attributes, handle inheritance, environment, working directory, startup information, command line, and caller-owned process handles.
- Forward caller-requested `CREATE_SUSPENDED` launches unchanged because Chromium still owns their sandbox setup.
- Reject inherited debugger flags because Floggfy cannot safely share ownership of the child's debug events.
- Terminate a covered child and return a native creation failure when pre-entry loading or verification fails.
- Leave automatic `version.dll` mode unchanged at the default mode `1`; mode `2` explicitly opts ordinary Spotify child launches into loading.
- Keep other executables and unsupported process-launch APIs outside this change.

## Approaches

The selected approach reuses the launcher's pre-entry debugger and loader sequence from an in-process `CreateProcessW` hook. A `version.dll` bootstrap was rejected because launcher mode exists for systems where the automatic proxy path does not initialize reliably. A Windows firewall rule was rejected because the feature must remain hook-only and portable beyond Windows-specific policy management.

## Verification

Automated Windows fixtures will prove pre-entry network-helper loading, suspended sandbox forwarding, unrelated-process forwarding, crash URL rewriting, and fail-closed cleanup. A Fiddler capture will compare released launcher behavior with the fixed launcher while `BlockTelemetry=0` and Network Kill Switch is enabled.
