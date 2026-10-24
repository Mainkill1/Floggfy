#!/usr/bin/env bash
# One focused Windows executable; no live Spotify or network access is needed.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/artwork-policy
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/artwork-policy/minimp4.o
floggfy_temp_win=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_artwork_root="$(wslpath -u "$floggfy_temp_win")/Floggfy-artwork-policy-tests"
mkdir -p "$floggfy_artwork_root"
python3 - <<'PY'
from pathlib import Path
script='window.__floggfyNative=true;\n'+Path('native/metadata_collector.js').read_text()
Path('build/metadata_script.h').write_text('static constexpr wchar_t metadata_script[]=LR"FLOGGFY('+script+')FLOGGFY";\n')
PY
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/artwork-policy/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/artwork-policy/vorbis.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/artwork-policy/ogg.o
# The test includes the actual metadata bridge and settings implementation.
# Link the capture worker too, so removed Media fields/providers cannot survive
# as unnoticed integration dependencies. Hooks and live SMTC reads are not run.
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ \
    -I native/vendor/libogg/include tests/windows_artwork_enrichment_test.cpp \
    native/audio_history.cpp native/media_session.cpp native/playback_quality_windows.cpp native/playback_quality.cpp \
    native/spotify_hook_discovery.cpp native/rich_metadata.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp \
    native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp native/image_info.cpp \
    native/recording_file.cpp native/aac_container.cpp native/spac_stream.cpp native/vorbis_headers.cpp native/existing_quality.cpp native/library_layout.cpp \
    native/file_publication.cpp native/metadata_update_core.cpp native/metadata_update_windows.cpp \
    native/recent_downloads.cpp native/song_destination.cpp native/async_log.cpp \
    build/artwork-policy/*.o -lole32 -luuid -lshell32 -lruntimeobject \
    -o "$floggfy_artwork_root/artwork-policy.exe"
exec "$floggfy_artwork_root/artwork-policy.exe"
