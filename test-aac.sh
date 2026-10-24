#!/usr/bin/env bash
# One Windows executable for optional AAC capture, container and publication.
# Optional arguments: a complete inspection SpAC file and the Spotify DLL to map
# for static capability discovery. Neither input is bundled with the project.
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [[ $# != 0 && $# != 2 ]]; then echo 'Usage: bash test-aac.sh [SPAC_FILE SPOTIFY_DLL]' >&2; exit 2; fi
mkdir -p build/aac-tests
floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_aac_root="$(wslpath -u "$floggfy_temp_win")/Floggfy-aac-tests-$(date +%s)-$$"
mkdir -p "$floggfy_aac_root"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/aac-tests/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/aac-tests/minimp4.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/aac-tests/vorbis.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/aac-tests/ogg.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -DFLOGGFY_COMBINED_TEST -municode \
    -static-libgcc -static-libstdc++ -I native/vendor/libogg/include \
    tests/windows_aac_capture_test.cpp tests/spac_stream_test.cpp tests/aac_container_test.cpp \
    native/spac_stream.cpp native/aac_container.cpp native/playback_quality_windows.cpp native/playback_quality.cpp native/media_session.cpp \
    native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp native/image_info.cpp \
    native/library_layout.cpp native/recording_file.cpp native/vorbis_headers.cpp native/existing_quality.cpp native/file_publication.cpp \
    native/lyrics_update.cpp native/metadata_update_core.cpp native/metadata_update_windows.cpp native/recent_downloads.cpp \
    native/song_destination.cpp native/spotify_hook_discovery.cpp native/async_log.cpp \
    build/aac-tests/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_aac_root/aac-test.exe"
if [[ $# == 2 ]]; then
    "$floggfy_aac_root/aac-test.exe" "$(wslpath -w "$1")" "$(wslpath -w "$floggfy_aac_root/source-sample.m4a")" "$(wslpath -w "$2")"
    echo "M4A inspection output: $floggfy_aac_root/source-sample.m4a"
else
    "$floggfy_aac_root/aac-test.exe"
fi
