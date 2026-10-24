#!/usr/bin/env bash
# One executable for destination lookup and CEF Song target binding.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/song-shortcut
floggfy_shortcut_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_shortcut_root="$(wslpath -u "$floggfy_shortcut_temp")/Floggfy-song-shortcut-$(date +%s)-$$"
mkdir -p "$floggfy_shortcut_root"
python3 tests/create_quality_fixtures.py "$floggfy_shortcut_root"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/song-shortcut/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/song-shortcut/minimp4.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/song-shortcut/vorbis.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/song-shortcut/ogg.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -DFLOGGFY_SONG_SHORTCUT_TEST \
    -static-libgcc -static-libstdc++ -I native/vendor/libogg/include \
    tests/windows_song_destination_test.cpp tests/windows_menu_quality_test.cpp tests/library_layout_test.cpp \
    native/library_layout.cpp native/existing_quality.cpp native/recording_file.cpp \
    native/aac_container.cpp native/spac_stream.cpp native/vorbis_headers.cpp \
    native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp \
    native/ogg_history_core.cpp native/ogg_tags.cpp native/image_info.cpp native/history_settings.cpp native/async_log.cpp \
    build/song-shortcut/*.o -lole32 -luuid -lshell32 -o "$floggfy_shortcut_root/song-shortcut-test.exe"
exec "$floggfy_shortcut_root/song-shortcut-test.exe"
