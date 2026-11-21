#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
bash build-artwork-codecs.sh gcc ar build/artwork-codecs-native
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/artwork_image_test.cpp \
    native/media/artwork_image.cpp native/media/image_info.cpp build/artwork-codecs-native/libwebpdecoder.a \
    -o build/artwork-image-test
build/artwork-image-test build/artwork-alpha-test.png
python3 tests/check_artwork_png.py build/artwork-alpha-test.png
g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/artwork_image_allocation_test.cpp \
    native/media/image_info.cpp build/artwork-codecs-native/libwebpdecoder.a \
    -o build/artwork-image-allocation-test
build/artwork-image-allocation-test
