#!/usr/bin/env bash
# All optional-diagnostics regressions. No Spotify execution or network requests.
# Optional DLL is mapped read-only for current-client discovery validation.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
if [[ $# -gt 1 ]]; then echo 'Usage: bash tests/runners/test-diagnostics.sh [SPOTIFY_DLL]' >&2; exit 2; fi
floggfy_diag_root=$(windows_test_dir "Floggfy-diagnostics-$(date +%s)-$$")
floggfy_diag_build=build/diagnostics-tests
mkdir -p "$floggfy_diag_build"
mkdir -p "$floggfy_diag_root/cap"
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" \
    tests/windows_log_test.cpp native/tooling/async_log.cpp native/tooling/history_settings.cpp \
    -lole32 -luuid -lshell32 -o "$floggfy_diag_root/cap/test.exe"
"$floggfy_diag_root/cap/test.exe"
common=("${windows_cxxflags[@]}" -municode)
mkdir -p "$floggfy_diag_root/settings"
x86_64-w64-mingw32-g++ "${windows_cxxflags[@]}" \
    tests/windows_settings_test.cpp -lole32 -luuid -lshell32 -o "$floggfy_diag_root/settings/test.exe"
for scenario in log-0 log-1 log-2 log-4 log-invalid log-padded log-duplicate legacy-log-zero legacy-debug-one; do
    "$floggfy_diag_root/settings/test.exe" "$scenario"
done
for component in networking_log renderer_privacy pr13_settings connect_settings; do
    mkdir -p "$floggfy_diag_root/$component"
    x86_64-w64-mingw32-g++ "${common[@]}" "tests/windows_${component}_test.cpp" \
        -lole32 -luuid -lshell32 -o "$floggfy_diag_root/$component/test.exe"
    "$floggfy_diag_root/$component/test.exe"
done
mkdir -p "$floggfy_diag_root/decoded-output"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_decoded_output_discovery_test.cpp \
    native/tooling/spotify_hook_discovery.cpp -lole32 -luuid -lshell32 -o "$floggfy_diag_root/decoded-output/test.exe"
if [[ $# == 1 ]]; then
    "$floggfy_diag_root/decoded-output/test.exe" "$(wslpath -w "$1")"
else
    "$floggfy_diag_root/decoded-output/test.exe"
fi
mkdir -p "$floggfy_diag_root/deep"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_deep_diagnostics_test.cpp \
    native/tooling/startup_launcher.cpp -lole32 -luuid -lshell32 -o "$floggfy_diag_root/deep/test.exe"
"$floggfy_diag_root/deep/test.exe"
build_windows_minhook "$floggfy_diag_build"
mkdir -p "$floggfy_diag_root/decoded"
x86_64-w64-mingw32-g++ "${common[@]}" -DFLOGGFY_COMBINED_TEST \
    tests/windows_decoded_connect_test.cpp tests/connect_message_test.cpp tests/connect_hook_discovery_test.cpp \
    native/media/connect_message.cpp native/tooling/spotify_hook_discovery.cpp "$floggfy_diag_build"/minhook-*.o \
    -lole32 -luuid -lshell32 -o "$floggfy_diag_root/decoded/test.exe"
if [[ $# == 1 ]]; then
    "$floggfy_diag_root/decoded/test.exe" "$(wslpath -w "$1")"
else
    "$floggfy_diag_root/decoded/test.exe"
fi
build_windows_audio_objects "$floggfy_diag_build"
mkdir -p "$floggfy_diag_root/playback"
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_playback_diagnostics_test.cpp tests/marked_catalog_stub.cpp \
    "${capture_sources[@]}" native/media/existing_quality.cpp native/ui/song_destination.cpp native/media/rich_metadata.cpp \
    "$floggfy_diag_build"/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_diag_root/playback/test.exe"
"$floggfy_diag_root/playback/test.exe"
node tests/collector_native_boundary_test.js --pending-prior-control |
    "$floggfy_diag_root/playback/test.exe" --pending-prior-control-stdin
for component in debug connect networking playback; do node "tests/collector_${component}_test.js"; done
python3 tests/diagnostics_source_test.py
python3 tests/decoder_ownership_log_test.py
