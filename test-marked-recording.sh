#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"

# Use the Windows temp volume so WSL can launch the test binary directly.
windows_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
test_dir=$(mktemp -d "$(wslpath -u "$windows_temp")/Floggfy-marked-test.XXXXXXXX")
trap 'rm -rf -- "$test_dir"' EXIT
cc=x86_64-w64-mingw32-gcc
cxx=x86_64-w64-mingw32-g++
"$cc" -std=c11 -O2 -c native/vendor/minimp4/minimp4.c -o "$test_dir/minimp4.o"
"$cc" -std=c11 -O2 -c native/vorbis_headers_stb.c -o "$test_dir/vorbis.o"
"$cc" -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o "$test_dir/ogg.o"
"$cxx" -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I native/vendor/libogg/include \
    tests/windows_marked_recording_test.cpp native/marked_recording.cpp native/marked_recording_windows.cpp \
    native/metadata_update_windows.cpp native/metadata_update_core.cpp native/recording_file.cpp \
    native/aac_container.cpp native/spac_stream.cpp native/mp3_stream.cpp native/vorbis_headers.cpp \
    native/existing_quality.cpp native/file_publication.cpp native/library_layout.cpp native/playback_quality.cpp \
    native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp \
    native/ogg_history_core.cpp native/history_settings.cpp native/async_log.cpp \
    "$test_dir/minimp4.o" "$test_dir/vorbis.o" "$test_dir/ogg.o" \
    -lole32 -luuid -lshell32 -o "$test_dir/marked-recording-test.exe"
"$test_dir/marked-recording-test.exe"
