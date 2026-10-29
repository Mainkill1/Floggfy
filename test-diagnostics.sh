#!/usr/bin/env bash
# All optional-diagnostics regressions. No Spotify execution or network requests.
# Optional DLL is mapped read-only for current-client discovery validation.
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $# -gt 1 ]]; then echo 'Usage: bash test-diagnostics.sh [SPOTIFY_DLL]' >&2; exit 2; fi
floggfy_diag_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_diag_root="$(wslpath -u "$floggfy_diag_temp")/Floggfy-diagnostics-$(date +%s)-$$"
floggfy_diag_build=build/diagnostics-tests
mkdir -p "$floggfy_diag_root" "$floggfy_diag_build"
mkdir -p "$floggfy_diag_root/cap"
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    tests/windows_log_test.cpp native/async_log.cpp native/history_settings.cpp \
    -lole32 -luuid -lshell32 -o "$floggfy_diag_root/cap/test.exe"
"$floggfy_diag_root/cap/test.exe"
common=(-std=c++17 -O2 -Wall -Wextra -Werror -municode -static-libgcc -static-libstdc++)
for component in networking_log renderer_privacy pr13_settings connect_settings; do
    mkdir -p "$floggfy_diag_root/$component"
    x86_64-w64-mingw32-g++ "${common[@]}" "tests/windows_${component}_test.cpp" \
        -lole32 -luuid -lshell32 -o "$floggfy_diag_root/$component/test.exe"
    "$floggfy_diag_root/$component/test.exe"
done
mkdir -p "$floggfy_diag_root/deep"
x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_deep_diagnostics_test.cpp \
    native/startup_launcher.cpp -lole32 -luuid -lshell32 -o "$floggfy_diag_root/deep/test.exe"
"$floggfy_diag_root/deep/test.exe"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "$floggfy_diag_build/minhook-${source//\//-}.o"
done
mkdir -p "$floggfy_diag_root/decoded"
x86_64-w64-mingw32-g++ "${common[@]}" -DFLOGGFY_COMBINED_TEST \
    tests/windows_decoded_connect_test.cpp tests/connect_message_test.cpp tests/connect_hook_discovery_test.cpp \
    native/connect_message.cpp native/spotify_hook_discovery.cpp "$floggfy_diag_build"/minhook-*.o \
    -lole32 -luuid -lshell32 -o "$floggfy_diag_root/decoded/test.exe"
if [[ $# == 1 ]]; then
    "$floggfy_diag_root/decoded/test.exe" "$(wslpath -w "$1")"
else
    "$floggfy_diag_root/decoded/test.exe"
fi
x86_64-w64-mingw32-gcc -std=c11 -O2 -c native/vendor/minimp4/minimp4.c -o "$floggfy_diag_build/minimp4.o"
x86_64-w64-mingw32-gcc -std=c11 -O2 -c native/vorbis_headers_stb.c -o "$floggfy_diag_build/vorbis.o"
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o "$floggfy_diag_build/ogg.o"
mkdir -p "$floggfy_diag_root/playback"
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include tests/windows_playback_diagnostics_test.cpp \
    native/spac_stream.cpp native/aac_container.cpp native/playback_quality_windows.cpp native/playback_quality.cpp native/media_session.cpp \
    native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp native/image_info.cpp \
    native/library_layout.cpp native/recording_file.cpp native/vorbis_headers.cpp native/existing_quality.cpp native/file_publication.cpp \
    native/lyrics_update.cpp native/metadata_update_core.cpp native/metadata_update_windows.cpp native/recent_downloads.cpp \
    native/song_destination.cpp native/spotify_hook_discovery.cpp native/async_log.cpp \
    "$floggfy_diag_build"/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_diag_root/playback/test.exe"
"$floggfy_diag_root/playback/test.exe"
for component in debug connect networking playback; do node "tests/collector_${component}_test.js"; done
python3 tests/diagnostics_source_test.py
