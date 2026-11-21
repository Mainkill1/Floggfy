#!/usr/bin/env bash
# Build only the portable WebP decoder; called by production and focused tests.
set -euo pipefail
cd -- "$(dirname -- "$0")"
floggfy_codec_cc="${1:?C compiler}"
floggfy_codec_ar="${2:?Archiver}"
floggfy_codec_dir="${3:?Output directory}"
floggfy_codec_flags=(-std=c11 -O2 -DHAVE_CONFIG_H -I native/vendor/libwebp)
case "${4:-test}" in
    release) floggfy_codec_flags+=(-DNDEBUG) ;;
    test) ;;
    *) echo "Expected WebP build mode: release or test" >&2; exit 2 ;;
esac
mkdir -p "$floggfy_codec_dir"
mapfile -t floggfy_codec_sources < native/vendor/libwebp/sources.txt
# One conservative fingerprint keeps no-op builds cheap without a dependency
# graph. Any source, header/config, tool or build-command change rebuilds all.
shopt -s globstar nullglob
floggfy_codec_inputs=("${0##*/}" native/vendor/libwebp/sources.txt native/vendor/libwebp/**/*.h)
for source in "${floggfy_codec_sources[@]}"; do
    floggfy_codec_inputs+=("native/vendor/libwebp/$source")
done
floggfy_codec_fingerprint=$({
    printf '%s\n' "$floggfy_codec_cc" "$floggfy_codec_ar" "${floggfy_codec_flags[@]}"
    sha256sum "$(command -v "$floggfy_codec_cc")" "$(command -v "$floggfy_codec_ar")" "${floggfy_codec_inputs[@]}" &&
    "$floggfy_codec_cc" --version &&
    "$floggfy_codec_ar" --version
} | sha256sum)
floggfy_codec_stamp="$floggfy_codec_dir/inputs.sha256"
floggfy_codec_archive="$floggfy_codec_dir/libwebpdecoder.a"
if [[ -f "$floggfy_codec_archive" && -f "$floggfy_codec_stamp" &&
      "$(cat "$floggfy_codec_stamp")" == "$floggfy_codec_fingerprint" ]]; then
    exit 0
fi
rm -f -- "$floggfy_codec_stamp"
floggfy_codec_objects=()
for source in "${floggfy_codec_sources[@]}"; do
    object="$floggfy_codec_dir/${source//\//-}.o"
    "$floggfy_codec_cc" "${floggfy_codec_flags[@]}" \
        -c "native/vendor/libwebp/$source" -o "$object"
    floggfy_codec_objects+=("$object")
done
# Start with a fresh archive so removed source files cannot leave stale members.
floggfy_codec_temporary=$(mktemp -d "$floggfy_codec_dir/.archive.XXXXXX")
trap 'rm -rf -- "$floggfy_codec_temporary"' EXIT
"$floggfy_codec_ar" rcs "$floggfy_codec_temporary/libwebpdecoder.a" "${floggfy_codec_objects[@]}"
printf '%s\n' "$floggfy_codec_fingerprint" > "$floggfy_codec_temporary/inputs.sha256"
mv -- "$floggfy_codec_temporary/libwebpdecoder.a" "$floggfy_codec_archive"
mv -- "$floggfy_codec_temporary/inputs.sha256" "$floggfy_codec_stamp"
