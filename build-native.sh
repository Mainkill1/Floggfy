#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build
python3 - <<'PY'
from pathlib import Path
script='window.__floggfyNative=true;\n'+Path('native/metadata_collector.js').read_text()
Path('build/metadata_script.h').write_text('static constexpr wchar_t metadata_script[]=LR"FLOGGFY('+script+')FLOGGFY";\n')
PY
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include \
    -c native/vendor/libogg/src/framing.c -o build/ogg-framing.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -shared \
    -static-libgcc -static-libstdc++ \
    -Wl,--no-insert-timestamp,--dynamicbase,--nxcompat \
    native/version_proxy.cpp native/pe_imports.cpp native/spotify_hook_discovery.cpp native/audio_history.cpp native/playback_quality.cpp native/playback_quality_windows.cpp native/ogg_history_core.cpp \
    native/media_session.cpp native/ogg_tags.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/history_settings.cpp native/library_layout.cpp \
    native/existing_quality.cpp native/file_publication.cpp native/to_disk_menu.cpp native/rich_metadata.cpp native/cached_metadata.cpp native/cached_metadata_windows.cpp native/metadata_bridge.cpp native/async_log.cpp native/version_exports.S native/version.def \
    build/minhook-*.o build/ogg-framing.o -I native/vendor/libogg/include \
    -o build/version.dll -lole32 -luuid -liphlpapi -lws2_32 -lruntimeobject -lshell32
# PE rewriting by strip otherwise inserts the current time even though the
# linker timestamp is disabled, producing a different DLL hash on every build.
SOURCE_DATE_EPOCH=1 x86_64-w64-mingw32-strip --strip-unneeded build/version.dll

x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -mwindows \
    -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp,--dynamicbase,--nxcompat \
    native/floggfy_launcher.cpp native/startup_launcher.cpp -o build/Floggfy.exe
SOURCE_DATE_EPOCH=1 x86_64-w64-mingw32-strip --strip-unneeded build/Floggfy.exe
