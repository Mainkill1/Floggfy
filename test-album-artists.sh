#!/usr/bin/env bash
# One focused invocation for album-artist precedence and legacy folder reuse.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/album-artists
python3 - <<'PY'
from pathlib import Path
script='window.__floggfyNative=true;\n'+Path('native/metadata_collector.js').read_text()
Path('build/metadata_script.h').write_text('static constexpr wchar_t metadata_script[]=LR"FLOGGFY('+script+')FLOGGFY";\n')
PY
floggfy_album_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_album_root="$(wslpath -u "$floggfy_album_temp")/Floggfy-album-artists-$(date +%s)-$$"
mkdir -p "$floggfy_album_root"
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/album-artists/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/album-artists/minimp4.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/album-artists/vorbis.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/album-artists/ogg.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -static-libgcc -static-libstdc++ \
    -I native/vendor/libogg/include tests/windows_album_artist_test.cpp \
    native/rich_metadata.cpp native/cached_metadata.cpp native/async_log.cpp native/media_session.cpp \
    native/image_info.cpp native/playback_quality_windows.cpp native/playback_quality.cpp native/flac_history_core.cpp \
    native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp native/library_layout.cpp \
    native/existing_quality.cpp native/recording_file.cpp native/file_publication.cpp native/metadata_update_core.cpp \
    native/aac_container.cpp native/spac_stream.cpp native/vorbis_headers.cpp \
    build/album-artists/*.o -lole32 -luuid -lshell32 -lruntimeobject -o "$floggfy_album_root/album-artist-test.exe"
node tests/album_artist_collector_test.js
exec "$floggfy_album_root/album-artist-test.exe"
