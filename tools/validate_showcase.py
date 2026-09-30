"""Independently validate native history bundles with Xiph libogg/libvorbisfile.

No FFmpeg. Runtime capture has no dependency on these development libraries.
Set VORBIS_LIBRARY_DIRECTORY when using libraries extracted into a private folder.
"""
import argparse
import base64
import ctypes as c
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import struct


class OggPage(c.Structure):
    _fields_ = [('header', c.c_void_p), ('header_len', c.c_long),
                ('body', c.c_void_p), ('body_len', c.c_long)]


class VorbisInfo(c.Structure):
    _fields_ = [('version', c.c_int), ('channels', c.c_int), ('rate', c.c_long),
                ('bitrate_upper', c.c_long), ('bitrate_nominal', c.c_long),
                ('bitrate_lower', c.c_long), ('bitrate_window', c.c_long),
                ('codec_setup', c.c_void_p)]


class VorbisComment(c.Structure):
    _fields_ = [('user_comments', c.POINTER(c.c_void_p)),
                ('comment_lengths', c.POINTER(c.c_int)), ('comments', c.c_int),
                ('vendor', c.c_char_p)]


def library(name):
    directory = os.environ.get('VORBIS_LIBRARY_DIRECTORY')
    soname = '3' if name == 'vorbisfile' else '0'
    path = str(Path(directory) / f'lib{name}.so.{soname}') if directory else ctypes.util.find_library(name)
    if not path:
        raise RuntimeError(f'Xiph lib{name} is required for independent verification')
    return c.CDLL(path, mode=c.RTLD_GLOBAL)


ogg = library('ogg')
vorbis = library('vorbis')
vf = library('vorbisfile')
ogg.ogg_page_checksum_set.argtypes = [c.POINTER(OggPage)]
vf.ov_fopen.argtypes = [c.c_char_p, c.c_void_p]
vf.ov_fopen.restype = c.c_int
vf.ov_clear.argtypes = [c.c_void_p]
vf.ov_info.argtypes = [c.c_void_p, c.c_int]
vf.ov_info.restype = c.POINTER(VorbisInfo)
vf.ov_comment.argtypes = [c.c_void_p, c.c_int]
vf.ov_comment.restype = c.POINTER(VorbisComment)
vf.ov_streams.argtypes = [c.c_void_p]
vf.ov_streams.restype = c.c_long
vf.ov_pcm_total.argtypes = [c.c_void_p, c.c_int]
vf.ov_pcm_total.restype = c.c_int64
vf.ov_read.argtypes = [c.c_void_p, c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_int, c.POINTER(c.c_int)]
vf.ov_read.restype = c.c_long


def validate(path, raw=False):
    data = path.read_bytes()
    offset = pages = 0
    serial = previous = granule = rate = channels = None
    eos = False
    while offset < len(data):
        assert not eos, 'Trailing data after EOS'
        assert data[offset:offset+5] == b'OggS\0', 'Invalid Ogg page start'
        assert offset + 27 <= len(data), 'Truncated header'
        segments = data[offset+26]
        header_length = 27 + segments
        assert offset + header_length <= len(data), 'Truncated lacing table'
        body_length = sum(data[offset+27:offset+header_length])
        end = offset + header_length + body_length
        assert end <= len(data), 'Truncated body'
        header = c.create_string_buffer(data[offset:offset+header_length], header_length)
        body = c.create_string_buffer(data[offset+header_length:end], max(1, body_length))
        expected = data[offset+22:offset+26]
        page = OggPage(c.addressof(header), header_length, c.addressof(body), body_length)
        ogg.ogg_page_checksum_set(c.byref(page))
        assert header.raw[22:26] == expected, f'CRC failure at page {pages}'
        flags = data[offset+5]
        current_granule, current_serial, sequence = struct.unpack_from('<qII', data, offset+6)
        if pages == 0:
            assert flags & 2 and not flags & 1, 'Missing complete Vorbis BOS'
            identification = data[offset+header_length:end]
            assert identification[:7] == b'\x01vorbis', 'Not Vorbis audio'
            channels = identification[11]
            rate = struct.unpack_from('<I', identification, 12)[0]
            serial = current_serial
        else:
            assert current_serial == serial and sequence == (previous + 1) & 0xffffffff, 'Page sequence gap'
            assert not flags & 2, 'Unexpected second BOS'
        if current_granule >= 0:
            assert granule is None or current_granule >= granule, 'Backward granule'
            granule = current_granule
        previous = sequence
        eos = bool(flags & 4)
        offset = end
        pages += 1
    assert eos and granule and rate, 'Missing complete EOS'
    state = c.create_string_buffer(8192)  # aligned, larger than OggVorbis_File
    result = vf.ov_fopen(os.fsencode(path), state)
    assert result == 0, f'libvorbisfile rejected file ({result})'
    try:
        assert vf.ov_streams(state) == 1, 'Unexpected chained stream'
        info = vf.ov_info(state, -1).contents
        assert info.rate == rate and info.channels == channels, 'Decoder/header format mismatch'
        comments = vf.ov_comment(state, -1).contents
        tags = {}
        for i in range(comments.comments):
            field = c.string_at(comments.user_comments[i], comments.comment_lengths[i]).decode('utf-8')
            if '=' in field:
                key, value = field.split('=', 1)
                tags[key.upper()] = value
        total = vf.ov_pcm_total(state, -1)
        assert total == granule, 'Decoder sample count differs from EOS'
        pcm = c.create_string_buffer(32768)
        section = c.c_int()
        decoded_bytes = 0
        digest = hashlib.sha256()
        nonzero = False
        while True:
            n = vf.ov_read(state, pcm, len(pcm), 0, 2, 1, c.byref(section))
            assert n >= 0, f'Vorbis decode error or hole ({n})'
            if n == 0:
                break
            block = pcm.raw[:n]
            decoded_bytes += n
            digest.update(block)
            nonzero |= block != bytes(n)
        decoded_samples = decoded_bytes // (channels * 2)
        assert decoded_samples == total and nonzero, 'Incomplete or silent decoded sample'
    finally:
        vf.ov_clear(state)
    if not raw:
        assert tags['HISTORY_COMPLETE_LISTEN'] == '1' and tags['HISTORY_TRANSCODED'] == '0'
        assert int(tags['HISTORY_SAMPLES']) == total and int(tags['HISTORY_SAMPLE_RATE']) == rate
        assert abs(float(tags['HISTORY_MEDIA_DURATION']) - total/rate) <= 1, 'Track association duration mismatch'
        for key in ['TITLE', 'ARTIST', 'ALBUM', 'METADATA_BLOCK_PICTURE']:
            assert tags.get(key), f'Missing embedded {key}'
    image = b''
    mime = None
    if 'METADATA_BLOCK_PICTURE' in tags:
        picture = base64.b64decode(tags['METADATA_BLOCK_PICTURE'], validate=True)
        cursor = 0
        def number():
            nonlocal cursor
            value = struct.unpack_from('>I', picture, cursor)[0]
            cursor += 4
            return value
        assert number() == 3, 'Not front cover artwork'
        length = number()
        mime = picture[cursor:cursor+length].decode('ascii')
        cursor += length
        description_length = number()
        cursor += description_length
        dimensions = [number() for _ in range(4)]
        length = number()
        image = picture[cursor:cursor+length]
        assert cursor+length == len(picture) and length > 0, 'Invalid embedded picture length'
        assert mime in ['image/png', 'image/jpeg'], 'Invalid artwork MIME type'
        assert (mime == 'image/png' and image.startswith(b'\x89PNG\r\n\x1a\n')) or (mime == 'image/jpeg' and image.startswith(b'\xff\xd8\xff')), 'Invalid artwork bytes'
    return {
        'title': tags.get('TITLE'), 'artist': tags.get('ARTIST'), 'album': tags.get('ALBUM'), 'path': str(path),
        'album_artist': tags.get('ALBUMARTIST'), 'track_number': int(tags.get('TRACKNUMBER', '0')),
        'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
        'duration_seconds': total/rate, 'sample_rate': rate, 'channels': channels,
        'ogg_pages': pages, 'crc_valid': True, 'bos_eos_valid': True,
        'decoded_samples': decoded_samples, 'decode_errors': 0,
        'decoded_pcm_sha256': digest.hexdigest(), 'artwork_bytes': len(image),
        'artwork_sha256': hashlib.sha256(image).hexdigest(), 'artwork_mime': mime,
        'embedded_tags': sorted(tags), 'complete_listen': tags.get('HISTORY_COMPLETE_LISTEN') == '1',
    }


parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
parser.add_argument('--required', type=int, default=3)
parser.add_argument('--report', type=Path)
parser.add_argument('--raw', action='store_true', help='Validate audio without requiring history tags')
args = parser.parse_args()
files = [args.directory] if args.directory.is_file() else sorted(args.directory.rglob('*.ogg'))
assert len(files) == args.required, f'Expected {args.required} complete samples; found {len(files)}'
results = [validate(path, args.raw) for path in files]
assert len({row['sha256'] for row in results}) == len(results), 'Duplicate samples'
report = {'validator': 'Xiph libogg CRC + libvorbisfile full PCM decode; no FFmpeg',
          'sample_count': len(results), 'samples': results}
if args.report:
    args.report.write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
