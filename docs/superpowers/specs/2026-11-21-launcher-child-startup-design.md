# Launcher Child Startup Design

## Intent

Launcher mode must give every Spotify process the same Floggfy coverage as the first process. The released launcher loads `Floggfy.dll` into the first `Spotify.exe` before its entry point, but Spotify creates its network helper later and that helper currently starts without Floggfy.

## Design

Floggfy will own one `CreateProcessW` detour inside each launcher-loaded Spotify process. For an approved executable in the same Spotify installation, the detour creates the child under the debugger, stops it at its entry point, loads the adjacent `Floggfy.dll` with the existing validated loader, verifies the exact module path, and then restores the caller's requested running or suspended state. The loaded child installs the same detour, so coverage follows later descendants recursively.

The detour will also retain the existing crash-report URL rewrite. Keeping both behaviors behind one hook prevents competing MinHook detours on `CreateProcessW`.

## Scope

- Cover `Spotify.exe`, `crashpad_handler.exe`, `SpotifyLauncher.exe`, `spotify_cli.exe`, and `save-to-spotify.exe` only when the resolved executable is in the same directory as the loaded `Floggfy.dll`.
- Preserve process attributes, thread attributes, handle inheritance, environment, working directory, startup information, command line, and caller-owned process handles.
- Preserve an original `CREATE_SUSPENDED` request. Resume an originally running child after Floggfy initialization.
- Reject inherited debugger flags because Floggfy cannot safely share ownership of the child's debug events.
- Terminate a covered child and return a native creation failure when pre-entry loading or verification fails.
- Leave automatic `version.dll` mode unchanged. Recursive child loading is enabled only when the module is named `Floggfy.dll`.
- Keep other executables and unsupported process-launch APIs outside this change.

## Approaches

The selected approach reuses the launcher's pre-entry debugger and loader sequence from an in-process `CreateProcessW` hook. A `version.dll` bootstrap was rejected because launcher mode exists for systems where the automatic proxy path does not initialize reliably. A Windows firewall rule was rejected because the feature must remain hook-only and portable beyond Windows-specific policy management.

## Verification

Automated Windows fixtures will prove pre-entry loading, recursive descendant coverage, original suspension preservation, unrelated-process forwarding, crash URL rewriting, and fail-closed cleanup. A Fiddler capture will then compare released launcher behavior with the fixed launcher while `BlockTelemetry=0` and Network Kill Switch is enabled.
