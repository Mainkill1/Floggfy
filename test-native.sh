#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build
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
