# puff inflate decoder

Unmodified `contrib/puff/puff.c` and `puff.h` from zlib v1.3.1, puff version 2.3.
Source: https://github.com/madler/zlib/tree/v1.3.1/contrib/puff
Copyright and license are retained in puff.h. This small inflate-only decoder is
used by opt-in telemetry diagnostics on the log writer, with a bounded output
buffer. It adds no external DLL requirement. The gzip envelope/checksum and
protobuf interpretation are handled separately by Floggfy.
