#!/usr/bin/env bash
# Replays local DumpStreams bytes through MP3 boundaries or ownership/quality.
# The raw capture remains local and is never copied into the repository.
set -euo pipefail
cd -- "$(dirname -- "$0")"
replay_mode=--dump-replay
replay_args=2
if [[ ${1:-} == --capture-buffer && $# == 3 ]]; then
    replay_mode=--capture-buffer-replay
    shift
fi
if [[ ${1:-} == --ogg-listen && $# == 3 ]]; then
    replay_mode=--dump-ogg-listen-replay
    shift
fi
if [[ ${1:-} == --quality && $# == 5 ]]; then
    replay_mode=--dump-quality-replay
    replay_args=4
    shift
fi
if [[ $# != "$replay_args" ]] || [[ ! -f $1 ]] || [[ $replay_mode != --dump-ogg-listen-replay && $replay_mode != --capture-buffer-replay && ! -f $2 ]]; then
    echo 'Usage: bash test-dump-replay.sh [--quality] STREAM.bin STREAM.jsonl [NATIVE_FIELD_HEX_OR_auto SPOTIFY_URI]; or --ogg-listen SESSION.jsonl SPOTIFY_URI; or --capture-buffer SESSION.jsonl OUTPUT_DIRECTORY' >&2
    exit 2
fi

floggfy_replay_temp=$(cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')
floggfy_replay_root="$(wslpath -u "$floggfy_replay_temp")/Floggfy-dump-replay-$(date +%s)-$$"
floggfy_replay_build=build/dump-replay-tests
mkdir -p "$floggfy_replay_root" "$floggfy_replay_build"

for source in buffer hook trampoline hde/hde64; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -c \
        "native/vendor/minhook/src/$source.c" -o "$floggfy_replay_build/minhook-${source//\//-}.o"
done
x86_64-w64-mingw32-gcc -std=c11 -O2 -c native/vendor/minimp4/minimp4.c -o "$floggfy_replay_build/minimp4.o"
x86_64-w64-mingw32-gcc -std=c11 -O2 -c native/vorbis_headers_stb.c -o "$floggfy_replay_build/vorbis.o"
x86_64-w64-mingw32-gcc -std=c11 -O2 -I native/vendor/libogg/include -c \
    native/vendor/libogg/src/framing.c -o "$floggfy_replay_build/ogg.o"

common=(-std=c++17 -O2 -Wall -Wextra -Werror -municode -static-libgcc -static-libstdc++)
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include \
    tests/windows_playback_diagnostics_test.cpp \
    native/spac_stream.cpp native/mp3_stream.cpp native/aac_container.cpp \
    native/playback_quality_windows.cpp native/playback_quality.cpp native/media_session.cpp \
    native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp \
    native/ogg_history_core.cpp native/image_info.cpp native/library_layout.cpp \
    native/recording_file.cpp native/vorbis_headers.cpp native/existing_quality.cpp \
    native/file_publication.cpp native/metadata_update_core.cpp native/metadata_update_windows.cpp \
    native/recent_downloads.cpp native/song_destination.cpp native/spotify_hook_discovery.cpp \
    native/playback_identity_bridge.cpp native/auto_skip.cpp native/async_log.cpp \
    native/decoder_ownership.cpp native/rich_metadata.cpp \
    "$floggfy_replay_build"/*.o -lole32 -luuid -lshell32 -lruntimeobject \
    -o "$floggfy_replay_root/test.exe"

if [[ $replay_mode == --capture-buffer-replay ]]; then
    "$floggfy_replay_root/test.exe" "$replay_mode" "$(wslpath -w "$1")" "$(wslpath -w "$2")"
elif [[ $replay_mode == --dump-ogg-listen-replay ]]; then
    "$floggfy_replay_root/test.exe" "$replay_mode" "$(wslpath -w "$1")" "$2"
elif [[ $replay_mode == --dump-quality-replay ]]; then
    "$floggfy_replay_root/test.exe" "$replay_mode" "$(wslpath -w "$1")" "$(wslpath -w "$2")" "$3" "$4"
else
    "$floggfy_replay_root/test.exe" "$replay_mode" "$(wslpath -w "$1")" "$(wslpath -w "$2")"
fi
