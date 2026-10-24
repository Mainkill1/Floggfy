#!/usr/bin/env bash
# One focused event-ordering test. Does not launch Spotify or write music files.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/capture-association
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/capture-association/minimp4.o
floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_association_root="$(wslpath -u "$floggfy_temp_win")/Floggfy-association-tests"
mkdir -p "$floggfy_association_root"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/capture-association/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/capture-association/vorbis.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/capture-association/ogg.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    -I native/vendor/libogg/include tests/windows_quality_observer_test.cpp \
    native/playback_quality_windows.cpp native/playback_quality.cpp native/media_session.cpp \
    native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp native/image_info.cpp \
    native/library_layout.cpp native/recording_file.cpp native/aac_container.cpp native/spac_stream.cpp native/vorbis_headers.cpp native/existing_quality.cpp native/file_publication.cpp \
    native/lyrics_update.cpp native/metadata_update_core.cpp native/metadata_update_windows.cpp native/recent_downloads.cpp \
    native/song_destination.cpp native/spotify_hook_discovery.cpp native/history_settings.cpp native/async_log.cpp \
    build/capture-association/*.o -lole32 -luuid -lshell32 -lruntimeobject \
    -o "$floggfy_association_root/capture-association.exe"
exec "$floggfy_association_root/capture-association.exe" --association
