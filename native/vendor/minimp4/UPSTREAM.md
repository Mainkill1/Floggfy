minimp4, revision `2a3014a7753cfcba4442adeb469431967238fe6a`.
https://github.com/lieff/minimp4/blob/2a3014a7753cfcba4442adeb469431967238fe6a/minimp4.h
Original SHA256: `9d122076cc23e5f4a7560ce4056c7d84484a8b8bf094d27c8f652f11fc725ce7`.
License: upstream public-domain/CC0 dedication retained in source and LICENSE.

The separate C11 translation unit enables only the MP4 muxer. It supplies
operation-local bounded allocation (128 MiB), never calls a decoder, and
never performs file/network I/O. The C++ adapter supplies bounded callbacks,
one regular audio track, ASC <=64 bytes, <=2^20 access units, <=512 MiB output,
and <=6 hours of presentation. This bounds muxer products and durations.

Documented source changes: normalize line endings; add a guard excluding
video helpers and the permissive demuxer; replace strdup with portable bounded
malloc/copy; free the temporary index on a failed backpatch callback; fold
write the reserved descriptor bit and mandatory SLConfigDescriptor; fold
rates >65535 in the legacy 16.16 sample-entry field (exact ASC/mdhd retained).
No warning suppression. Compile minimp4.c as C11 with -O2 -Wall -Wextra;
project C++ retains its normal warnings. No other minimp4 API is called.

ASC <=64 also keeps every nested MPEG-4 descriptor length below128, avoiding
the upstream writer's nonstandard multi-byte length encoding. A separate
strict box/table reader validates the supported output layout and metadata.
It does not claim to validate or decode compressed AAC access-unit contents.
