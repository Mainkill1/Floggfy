"""Independent FLAC verification using the Xiph decoder CLI, for independent validation."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def metadata_blocks(source, digest):
    while True:
        header = source.read(4)
        assert len(header) == 4, 'Truncated metadata header'
        length = int.from_bytes(header[1:], 'big')
        payload = source.read(length)
        assert len(payload) == length, 'Truncated metadata payload'
        digest.update(header)
        digest.update(payload)
        yield header[0] & 127, payload
        if header[0] & 128:
            return


def validate(path, executable, raw=False, require_artwork=False):
    tags = {}
    pictures = []
    with path.open('rb') as source:
        signature = source.read(4)
        assert signature == b'fLaC', 'Missing native FLAC signature'
        encoded_digest = hashlib.sha256(signature)
        blocks = metadata_blocks(source, encoded_digest)
        kind, stream = next(blocks)
        assert kind == 0 and len(stream) == 34, 'Missing STREAMINFO'
        packed = int.from_bytes(stream[10:18], 'big')
        rate = packed >> 44
        channels = ((packed >> 41) & 7) + 1
        bits = ((packed >> 36) & 31) + 1
        samples = packed & 0xfffffffff
        assert rate and samples, 'Unknown sample count'
        for kind, payload in blocks:
            assert kind != 0, 'Duplicate STREAMINFO'
            if kind==4:
                n=int.from_bytes(payload[:4],'little'); p=4+n
                assert p+4<=len(payload), 'Truncated vendor'
                count=int.from_bytes(payload[p:p+4],'little'); p+=4
                for _ in range(count):
                    assert p+4<=len(payload), 'Truncated comment length'
                    n=int.from_bytes(payload[p:p+4],'little');p+=4
                    assert p+n<=len(payload), 'Truncated comment'
                    field=payload[p:p+n].decode('utf-8');p+=n
                    if '=' in field:
                        key,value=field.split('=',1);tags[key.upper()]=value
                assert p==len(payload), 'Trailing comment bytes'
            elif kind==6:
                p=0
                def number():
                    nonlocal p
                    value=struct.unpack_from('>I',payload,p)[0]; p+=4;return value
                picture_type=number();n=number();mime=payload[p:p+n].decode('ascii');p+=n
                n=number();p+=n
                dimensions=[number() for _ in range(4)]
                n=number();image=payload[p:p+n];p+=n
                assert p==len(payload) and n>0, 'Truncated picture'
                assert mime in ['image/png','image/jpeg'], 'Unsupported picture MIME'
                assert (mime=='image/png' and image.startswith(b'\x89PNG\r\n\x1a\n')) or (mime=='image/jpeg' and image.startswith(b'\xff\xd8\xff')), 'Picture signature mismatch'
                pictures.append({'type':picture_type,'bytes':len(image),'mime':mime,'sha256':hashlib.sha256(image).hexdigest()})

        for block in iter(lambda: source.read(65536), b''):
            encoded_digest.update(block)
        encoded_size = source.tell()

    checked=subprocess.run([executable,'--silent','--test',str(path)],capture_output=True)
    assert checked.returncode==0, f'FLAC decoder validation failed: {checked.stderr.decode()}'
    process=subprocess.Popen([executable,'--silent','--decode','--stdout','--force-raw-format','--endian=little','--sign=signed',str(path)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    sha=hashlib.sha256(); md5=hashlib.md5(); count=0;nonzero=False
    while True:
        b=process.stdout.read(65536)
        if not b:break
        count+=len(b);sha.update(b);md5.update(b);nonzero|=b!=bytes(len(b))
    error=process.stderr.read(); code=process.wait()
    assert code==0, f'Full FLAC decode failed: {error.decode()}'
    assert count==samples*channels*((bits+7)//8) and nonzero, 'Decoded sample count or silence mismatch'
    supplied_md5=stream[18:34]!=bytes(16)
    if supplied_md5:assert md5.digest()==stream[18:34], 'Original STREAMINFO audio MD5 mismatch'
    if not raw:
        for key in ['TITLE','ARTIST','ALBUM']:assert tags.get(key), f'Missing {key}'
        assert tags['HISTORY_COMPLETE_LISTEN']=='1' and tags['HISTORY_TRANSCODED']=='0'
        assert int(tags['HISTORY_SAMPLES'])==samples and int(tags['HISTORY_SAMPLE_RATE'])==rate
        assert int(tags['HISTORY_BITS_PER_SAMPLE'])==bits
        assert abs(float(tags['HISTORY_MEDIA_DURATION'])-samples/rate)<=1
    if require_artwork:
        assert any(p['type']==3 for p in pictures), 'Missing embedded front cover'
    return {'path':str(path),'title':tags.get('TITLE'),'artist':tags.get('ARTIST'),'album':tags.get('ALBUM'),'album_artist':tags.get('ALBUMARTIST'),'track_number':int(tags.get('TRACKNUMBER','0')),'bytes':encoded_size,'sha256':encoded_digest.hexdigest(),'duration_seconds':samples/rate,'sample_rate':rate,'channels':channels,'bits_per_sample':bits,'decoded_samples':samples,'decode_errors':0,'decoded_pcm_sha256':sha.hexdigest(),'streaminfo_md5_supplied':supplied_md5,'streaminfo_md5_verified':supplied_md5,'pictures':pictures,'embedded_tags':sorted(tags),'complete_listen':tags.get('HISTORY_COMPLETE_LISTEN')=='1'}


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('path',type=Path);parser.add_argument('--flac',default='flac');parser.add_argument('--required',type=int,default=1);parser.add_argument('--raw',action='store_true');parser.add_argument('--report',type=Path);parser.add_argument('--require-artwork',action='store_true',help='Also require optional embedded cover artwork')
    args=parser.parse_args();files=[args.path] if args.path.is_file() else sorted(args.path.rglob('*.flac'))
    assert len(files)==args.required, f'Expected {args.required} files, found {len(files)}'
    rows=[validate(path,args.flac,args.raw,args.require_artwork) for path in files]
    report={'validator':'Xiph FLAC full decode, frame CRC and source STREAMINFO MD5 when supplied; no FFmpeg','sample_count':len(rows),'samples':rows}
    if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
