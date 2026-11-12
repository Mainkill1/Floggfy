#!/usr/bin/env bash
# Optional Windows large-file gate: ~1.1 GiB RAM and 513 MiB temporary disk.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/large-podcast
floggfy_large_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_large_root="$(wslpath -u "$floggfy_large_temp")/Floggfy-large-podcast-test-$(date +%s)-$$"
mkdir -p "$floggfy_large_root"
x86_64-w64-mingw32-gcc -std=c11 -O2 -c native/vendor/minimp4/minimp4.c -o build/large-podcast/minimp4.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -c native/vorbis_headers_stb.c -o build/large-podcast/vorbis.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/large-podcast/ogg.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    -I native/vendor/libogg/include tests/windows_large_podcast_test.cpp native/file_publication.cpp \
    native/existing_quality.cpp native/metadata_update_core.cpp native/recording_file.cpp native/aac_container.cpp \
    native/spac_stream.cpp native/mp3_stream.cpp native/vorbis_headers.cpp native/library_layout.cpp \
    native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp \
    native/ogg_tags.cpp native/ogg_history_core.cpp build/large-podcast/*.o \
    -o "$floggfy_large_root/test.exe"
exec "$floggfy_large_root/test.exe"
