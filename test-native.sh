#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/retry_schedule_test.cpp -o build/retry-schedule-test
build/retry-schedule-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/matcher_diagnostics_test.cpp -o build/matcher-diagnostics-test
build/matcher-diagnostics-test
python3 tests/analyze_match_timings_test.py
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/tag_memory_plan_test.cpp -o build/tag-memory-plan-test
build/tag-memory-plan-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/recording_memory_test.cpp -o build/recording-memory-test
build/recording-memory-test
gcc -std=c11 -O2 -Wall -Wextra -c native/vendor/minimp4/minimp4.c -o build/minimp4-native-test.o
gcc -std=c11 -O2 -Wall -Wextra -c native/vorbis_headers_stb.c -o build/vorbis-headers-stb-test.o
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/hook_init_state_test.cpp -o build/hook-init-state-test
build/hook-init-state-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/hook_rollback_test.cpp -o build/hook-rollback-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/hook_startup_batch_test.cpp -o build/hook-startup-batch-test
build/hook-startup-batch-test
build/hook-rollback-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/cef_menu_capability_test.cpp -o build/cef-menu-capability-test
build/cef-menu-capability-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/pe_imports_test.cpp native/pe_imports.cpp -o build/pe-imports-test
build/pe-imports-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/spotify_hook_discovery_test.cpp native/spotify_hook_discovery.cpp -o build/spotify-hook-discovery-test
build/spotify-hook-discovery-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/playback_identity_bridge_test.cpp native/playback_identity_bridge.cpp -o build/playback-identity-bridge-test
build/playback-identity-bridge-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/recording_counters_test.cpp -o build/recording-counters-test
build/recording-counters-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/decoder_ownership_test.cpp native/decoder_ownership.cpp -o build/decoder-ownership-test
build/decoder-ownership-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/history_core_test.cpp native/ogg_history_core.cpp -o build/history-core-test
build/history-core-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/capture_speed_test.cpp native/ogg_history_core.cpp -o build/capture-speed-test
build/capture-speed-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/decoded_output_speed_test.cpp -o build/decoded-output-speed-test
build/decoded-output-speed-test
gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/ogg-framing-test.o
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include \
    tests/ogg_tags_test.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/ogg-tags-test
build/ogg-tags-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/library_layout_test.cpp native/library_layout.cpp -o build/library-layout-test
build/library-layout-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/download_policy_test.cpp -o build/download-policy-test
build/download-policy-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include \
    tests/flac_history_test.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/flac-history-test
build/flac-history-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/compressed_buffer_test.cpp native/compressed_buffer.cpp -o build/compressed-buffer-test
build/compressed-buffer-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/compressed_buffer_allocation_test.cpp native/compressed_buffer.cpp -o build/compressed-buffer-allocation-test
build/compressed-buffer-allocation-test

g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/rich_metadata_test.cpp native/rich_metadata.cpp -o build/rich-metadata-test
build/rich-metadata-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/bounded_queue_test.cpp -o build/bounded-queue-test
build/bounded-queue-test
node tests/metadata_collector_test.js
node tests/auto_skip_collector_test.js
node tests/never_pause_collector_test.js

python3 tests/create_cache_fixtures.py build/cache-fixtures
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/cached_metadata_test.cpp native/cached_metadata.cpp native/rich_metadata.cpp -o build/cached-metadata-test
build/cached-metadata-test

g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include tests/playback_quality_test.cpp native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/playback-quality-test
build/playback-quality-test

g++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread tests/album_artwork_test.cpp native/album_artwork.cpp native/image_info.cpp -o build/album-artwork-test
build/album-artwork-test

# Existing files must contain complete audio and match the recording identity.
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include \
    tests/recording_file_test.cpp native/recording_file.cpp native/aac_container.cpp native/spac_stream.cpp native/mp3_stream.cpp build/minimp4-native-test.o native/vorbis_headers.cpp build/vorbis-headers-stb-test.o native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/recording-file-test
build/recording-file-test

g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include \
 tests/metadata_update_test.cpp native/metadata_update_core.cpp native/recording_file.cpp native/aac_container.cpp native/spac_stream.cpp native/mp3_stream.cpp build/minimp4-native-test.o native/vorbis_headers.cpp build/vorbis-headers-stb-test.o native/flac_history_core.cpp native/compressed_buffer.cpp native/image_info.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/metadata-update-test
build/metadata-update-test

# Supported AAC packets retain their configuration, timing and original bytes.
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/mp3_stream_test.cpp native/mp3_stream.cpp -o build/mp3-stream-test
build/mp3-stream-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/mp3_boundary_test.cpp native/mp3_stream.cpp native/compressed_buffer.cpp -o build/mp3-boundary-test
build/mp3-boundary-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/mp3_limits_test.cpp -o build/mp3-limits-test
build/mp3-limits-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/spac_stream_test.cpp native/spac_stream.cpp native/mp3_stream.cpp -o build/spac-stream-test
build/spac-stream-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/aac_container_test.cpp native/aac_container.cpp native/image_info.cpp build/minimp4-native-test.o -o build/aac-container-test
build/aac-container-test

g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/client_playback_test.cpp native/rich_metadata.cpp -o build/client-playback-test
build/client-playback-test
node tests/collector_native_boundary_test.js | build/client-playback-test --stdin
node tests/collector_native_boundary_test.js --active-without-uri | build/client-playback-test --active-without-uri-stdin
node tests/collector_native_boundary_test.js --transition | build/client-playback-test --transition-stdin
node tests/collector_native_boundary_test.js --reused-transition | build/client-playback-test --reused-transition-stdin
node tests/collector_native_boundary_test.js --terminal | build/client-playback-test --terminal-stdin
node tests/collector_playback_test.js
