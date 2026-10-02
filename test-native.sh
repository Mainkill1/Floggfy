#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/hook_init_state_test.cpp -o build/hook-init-state-test
build/hook-init-state-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/hook_installation_test.cpp -o build/hook-installation-test
build/hook-installation-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/hook_rollback_test.cpp -o build/hook-rollback-test
build/hook-rollback-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/cef_identity_test.cpp -o build/cef-identity-test
build/cef-identity-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/cef_menu_capability_test.cpp -o build/cef-menu-capability-test
build/cef-menu-capability-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/module_pending_test.cpp -o build/module-pending-test
build/module-pending-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/pe_imports_test.cpp native/pe_imports.cpp -o build/pe-imports-test
build/pe-imports-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/spotify_hook_discovery_test.cpp native/spotify_hook_discovery.cpp -o build/spotify-hook-discovery-test
build/spotify-hook-discovery-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/history_core_test.cpp native/ogg_history_core.cpp -o build/history-core-test
build/history-core-test
gcc -std=c11 -O2 -I native/vendor/libogg/include -c native/vendor/libogg/src/framing.c -o build/ogg-framing-test.o
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include \
    tests/ogg_tags_test.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/ogg-tags-test
build/ogg-tags-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/library_layout_test.cpp native/library_layout.cpp -o build/library-layout-test
build/library-layout-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include \
    tests/flac_history_test.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/flac-history-test
build/flac-history-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/compressed_buffer_test.cpp native/compressed_buffer.cpp -o build/compressed-buffer-test
build/compressed-buffer-test

g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/rich_metadata_test.cpp native/rich_metadata.cpp -o build/rich-metadata-test
build/rich-metadata-test
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/bounded_queue_test.cpp -o build/bounded-queue-test
build/bounded-queue-test
node tests/metadata_collector_test.js

python3 tests/create_cache_fixtures.py build/cache-fixtures
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/cached_metadata_test.cpp native/cached_metadata.cpp native/rich_metadata.cpp -o build/cached-metadata-test
build/cached-metadata-test

g++ -std=c++17 -O2 -Wall -Wextra -Werror -I native/vendor/libogg/include tests/playback_quality_test.cpp native/playback_quality.cpp native/flac_history_core.cpp native/compressed_buffer.cpp native/ogg_tags.cpp native/ogg_history_core.cpp build/ogg-framing-test.o -o build/playback-quality-test
build/playback-quality-test
