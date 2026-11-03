#!/usr/bin/env bash
# Deterministic mutation fuzzing and sanitizer regressions; no client execution.
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/capture-fuzz
flags=(-g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all)
gcc "${flags[@]}" -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/capture-fuzz/ogg.o
gcc "${flags[@]}" -c native/vendor/minimp4/minimp4.c -o build/capture-fuzz/minimp4.o
gcc "${flags[@]}" -c native/vorbis_headers_stb.c -o build/capture-fuzz/vorbis.o
sources=(native/recording_file.cpp native/aac_container.cpp native/spac_stream.cpp native/mp3_stream.cpp native/vorbis_headers.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp native/rich_metadata.cpp native/spotify_hook_discovery.cpp)
g++ -std=c++17 "${flags[@]}" -pthread -I native/vendor/libogg/include tests/capture_fuzz_test.cpp "${sources[@]}" build/capture-fuzz/*.o -o build/capture-fuzz/fuzz
env ASAN_OPTIONS=detect_leaks=1:allocator_may_return_null=1 build/capture-fuzz/fuzz "${1:-20000}" "${2:-4043931417}"
for test in history_core capture_speed decoded_output_speed compressed_buffer compressed_buffer_allocation client_playback spotify_hook_discovery hook_rollback hook_startup_batch bounded_queue; do
    extra=()
    case "$test" in history_core|capture_speed) extra=(native/ogg_history_core.cpp);; compressed_buffer|compressed_buffer_allocation) extra=(native/compressed_buffer.cpp);; esac
    case "$test" in client_playback) extra=(native/rich_metadata.cpp);; spotify_hook_discovery) extra=(native/spotify_hook_discovery.cpp);; esac
    g++ -std=c++17 "${flags[@]}" -pthread "tests/${test}_test.cpp" "${extra[@]}" -o "build/capture-fuzz/$test"
    "build/capture-fuzz/$test"
done
g++ -std=c++17 "${flags[@]}" tests/decoder_ownership_fuzz_test.cpp native/decoder_ownership.cpp \
    -o build/capture-fuzz/decoder_ownership_fuzz
build/capture-fuzz/decoder_ownership_fuzz "${1:-20000}" "${2:-4043931417}"
lifecycle_iterations="${1:-20000}"
if (( lifecycle_iterations > 2000 )); then lifecycle_iterations=2000; fi
node tests/collector_lifecycle_fuzz_test.js "$lifecycle_iterations" "${2:-4043931417}" |
    build/capture-fuzz/client_playback --fuzz-stream-stdin
