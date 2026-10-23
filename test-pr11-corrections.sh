#!/usr/bin/env bash
# Run exactly one focused correction test; there is deliberately no "all" mode.
set -euo pipefail
cd -- "$(dirname -- "$0")"
issue=${1:?Usage: bash test-pr11-corrections.sh ISSUE_NUMBER}
case "$issue" in 1|2|3|4|5|6) ;; *) echo 'Issue must be 1 through 6' >&2; exit 2;; esac
mkdir -p build/pr11-corrections
common=(-std=c++17 -O2 -Wall -Wextra -Werror)
if [[ "$issue" == 4 ]]; then
    g++ "${common[@]}" tests/history_core_test.cpp native/ogg_history_core.cpp -o build/pr11-corrections/issue4
    exec build/pr11-corrections/issue4 --issue4
fi
if [[ "$issue" == 6 ]]; then
    gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/pr11-corrections/vorbis-native.o
    gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/pr11-corrections/ogg-native.o
    g++ "${common[@]}" -pthread -I native/vendor/libogg/include tests/recording_file_test.cpp \
        native/recording_file.cpp native/vorbis_headers.cpp native/flac_history_core.cpp native/compressed_buffer.cpp \
        native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp \
        build/pr11-corrections/vorbis-native.o build/pr11-corrections/ogg-native.o -o build/pr11-corrections/issue6
    exec build/pr11-corrections/issue6 --issue6
fi
# Windows tests use an isolated executable directory beneath Windows TEMP.
floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_correction_root="$(wslpath -u "$floggfy_temp_win")/Floggfy-pr11-corrections"
mkdir -p "$floggfy_correction_root"
common+=(-static-libgcc -static-libstdc++)
case "$issue" in
1|3)
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/pr11-corrections/vorbis-windows.o
    x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/pr11-corrections/ogg-windows.o
    source=tests/windows_collision_reuse_test.cpp
    flags=()
    if [[ "$issue" == 1 ]]; then source=tests/windows_upgrade_metadata_test.cpp; flags=(-DFLOGGFY_PUBLICATION_TEST); fi
    x86_64-w64-mingw32-g++ "${common[@]}" "${flags[@]}" -I native/vendor/libogg/include "$source" \
        native/file_publication.cpp native/existing_quality.cpp native/metadata_update_core.cpp native/recording_file.cpp \
        native/vorbis_headers.cpp native/library_layout.cpp native/playback_quality.cpp native/flac_history_core.cpp \
        native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp \
        build/pr11-corrections/vorbis-windows.o build/pr11-corrections/ogg-windows.o -o "$floggfy_correction_root/issue$issue.exe"
    ;;
2)
    x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_thumbnail_cache_test.cpp native/thumbnail_cache.cpp native/image_info.cpp \
        -o "$floggfy_correction_root/issue2.exe"
    ;;
5)
    x86_64-w64-mingw32-g++ "${common[@]}" tests/windows_settings_failure_test.cpp -lole32 -luuid -lshell32 \
        -o "$floggfy_correction_root/issue5.exe"
    ;;
esac
exec "$floggfy_correction_root/issue$issue.exe"
