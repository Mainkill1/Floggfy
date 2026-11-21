libwebp 1.6.0 decoder-only subset

Source: https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
Archive SHA-256: e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564

Unmodified decoder/common DSP/common utils sources from upstream Makefile.am, plus headers.
COPYING, PATENTS and AUTHORS retained. The only added files are this note, sources.txt,
and a minimal src/webp/config.h selecting scalar decoding without worker threads.
No encoder, demuxer, animation, platform codec or separate runtime DLL is linked.
