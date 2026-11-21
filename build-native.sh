#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build
bash build-artwork-codecs.sh x86_64-w64-mingw32-gcc ar build/artwork-codecs-release release
# Only release objects disable vendor assertions and SQLite test controls.
x86_64-w64-mingw32-gcc -std=c11 -O2 -DNDEBUG -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/minimp4.o
python3 tools/embed_assets.py
# The Saved catalog uses ordinary indexed tables and a single connection.
x86_64-w64-mingw32-gcc -std=c11 -Os -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0 \
    -DSQLITE_UNTESTABLE -DSQLITE_OMIT_JSON -DSQLITE_OMIT_SHARED_CACHE -c native/vendor/sqlite/sqlite3.c -o build/sqlite3.o
for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "build/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include \
    -c native/vendor/libogg/src/framing.c -o build/ogg-framing.o
x86_64-w64-mingw32-gcc -std=c11 -O2 -DNDEBUG -Wall -Wextra -c native/media/vorbis_headers_stb.c -o build/vorbis-headers-stb.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -DNDEBUG -Wall -Wextra -shared \
    -static-libgcc -static-libstdc++ \
    -Wl,--no-insert-timestamp,--dynamicbase,--nxcompat \
    native/version_proxy.cpp native/tooling/update_blocker.cpp native/tooling/network_kill_switch.cpp native/tooling/network_reconnect.cpp native/tooling/startup_launcher.cpp native/tooling/process_startup_loader.cpp native/tooling/spotify_child_process.cpp native/telemetry/telemetry_crash.cpp native/telemetry/telemetry_queue.cpp native/telemetry/telemetry_queue_exit.cpp native/telemetry/telemetry_filter.cpp native/telemetry/telemetry_decode.cpp native/telemetry/telemetry_inflate.cpp native/tooling/pe_imports.cpp native/tooling/spotify_hook_discovery.cpp native/media/connect_message.cpp native/media/connect_messages_windows.cpp native/media/audio_history.cpp native/media/playback_identity_bridge.cpp native/media/auto_skip.cpp native/media/decoder_ownership.cpp native/media/playback_quality.cpp native/media/playback_quality_windows.cpp native/media/ogg_history_core.cpp \
    native/media/media_session.cpp native/media/image_info.cpp native/media/album_artwork.cpp native/media/ogg_tags.cpp native/media/flac_history_core.cpp native/media/compressed_buffer.cpp native/tooling/history_settings.cpp native/media/library_layout.cpp \
    native/media/recording_file.cpp native/media/aac_container.cpp native/media/spac_stream.cpp native/media/mp3_stream.cpp build/minimp4.o native/media/vorbis_headers.cpp build/vorbis-headers-stb.o native/media/existing_quality.cpp native/media/file_publication.cpp native/media/metadata_update_core.cpp native/media/metadata_update_windows.cpp native/ui/recent_downloads.cpp native/media/marked_recording.cpp native/media/marked_recording_windows.cpp native/ui/marked_catalog.cpp build/sqlite3.o native/ui/song_destination.cpp native/ui/to_disk_menu.cpp native/media/rich_metadata.cpp native/media/artwork_reply.cpp native/media/cached_metadata.cpp native/media/cached_metadata_windows.cpp native/media/cached_artwork_windows.cpp native/media/artwork_image.cpp native/media/metadata_bridge.cpp native/tooling/async_log.cpp native/version_exports.S native/version.def \
    build/minhook-*.o build/ogg-framing.o -I native/vendor/libogg/include \
    -o build/version.dll build/artwork-codecs-release/libwebpdecoder.a -lole32 -luuid -liphlpapi -lws2_32 -lruntimeobject -lshell32 -lwinhttp -ladvapi32
# PE rewriting by strip otherwise inserts the current time even though the
# linker timestamp is disabled, producing a different DLL hash on every build.
SOURCE_DATE_EPOCH=1 x86_64-w64-mingw32-strip --strip-unneeded build/version.dll

python3 tools/embed_icon.py
x86_64-w64-mingw32-windres -I . native/floggfy_launcher.rc -O coff -o build/launcher-resources.o
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror -municode -mwindows \
    -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp,--dynamicbase,--nxcompat \
    native/floggfy_launcher.cpp native/tooling/startup_launcher.cpp native/tooling/process_startup_loader.cpp build/launcher-resources.o -o build/Floggfy.exe
SOURCE_DATE_EPOCH=1 x86_64-w64-mingw32-strip --strip-unneeded build/Floggfy.exe
