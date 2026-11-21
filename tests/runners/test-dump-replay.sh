#!/usr/bin/env bash
# Replays local DumpStreams bytes through MP3 boundaries or ownership/quality.
# The raw capture remains local and is never copied into the repository.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
source tests/runners/windows-common.sh
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
    echo 'Usage: bash tests/runners/test-dump-replay.sh [--quality] STREAM.bin STREAM.jsonl [NATIVE_FIELD_HEX_OR_auto SPOTIFY_URI]; or --ogg-listen SESSION.jsonl SPOTIFY_URI; or --capture-buffer SESSION.jsonl OUTPUT_DIRECTORY' >&2
    exit 2
fi

floggfy_replay_root=$(windows_test_dir "Floggfy-dump-replay-$(date +%s)-$$")
floggfy_replay_build=build/dump-replay-tests
mkdir -p "$floggfy_replay_build"

build_windows_minhook "$floggfy_replay_build"
build_windows_audio_objects "$floggfy_replay_build"

common=("${windows_cxxflags[@]}" -municode)
python3 tools/embed_assets.py
x86_64-w64-mingw32-g++ "${common[@]}" -I native/vendor/libogg/include \
    tests/windows_playback_diagnostics_test.cpp tests/marked_catalog_stub.cpp \
    "${capture_sources[@]}" native/media/existing_quality.cpp native/ui/song_destination.cpp native/media/rich_metadata.cpp \
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
