libogg 1.3.5 from https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.xz
SHA256: c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705.

Only the framing source, CRC table, required headers and BSD license are
vendored. config_types.h supplies standard fixed width types for the Linux
test build; MinGW uses the upstream os_types.h Windows definitions.

This is container framing only, with no audio encoder or decoder.

A contact email was omitted from a nonlicense source comment; copyright and license notices remain intact.
