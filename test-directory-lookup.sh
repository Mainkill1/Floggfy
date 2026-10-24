#!/usr/bin/env bash
# One focused Windows executable for directory-first lookup, conversion and menu.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/directory-lookup
floggfy_names_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_names_root="$(wslpath -u "$floggfy_names_temp")/Floggfy-directory-lookup-$(date +%s)-$$"
mkdir -p "$floggfy_names_root"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/directory-lookup/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/directory-lookup/minimp4.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/directory-lookup/vorbis.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/directory-lookup/ogg.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -static-libgcc -static-libstdc++ \
    -I native/vendor/libogg/include tests/windows_directory_lookup_test.cpp \
    native/spac_stream.cpp native/aac_container.cpp native/playback_quality_windows.cpp native/playback_quality.cpp native/media_session.cpp \
    native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp native/image_info.cpp \
    native/library_layout.cpp native/recording_file.cpp native/vorbis_headers.cpp native/file_publication.cpp \
    native/lyrics_update.cpp native/metadata_update_core.cpp native/metadata_update_windows.cpp native/recent_downloads.cpp \
    native/spotify_hook_discovery.cpp native/async_log.cpp \
    build/directory-lookup/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_names_root/directory-lookup-test.exe"
exec "$floggfy_names_root/directory-lookup-test.exe"
