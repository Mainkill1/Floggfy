"""Independent FLAC verification using the Xiph decoder CLI, for independent validation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def validate(path, executable, raw=False):
    data=path.read_bytes()
    assert data[:4]==b'fLaC', 'Missing native FLAC signature'
    cursor=4; blocks=[]; last=False
    while not last:
        assert cursor+4<=len(data), 'Truncated metadata header'
        kind=data[cursor]&127; last=bool(data[cursor]&128)
        length=int.from_bytes(data[cursor+1:cursor+4], 'big'); cursor+=4
        assert cursor+length<=len(data), 'Truncated metadata payload'
        blocks.append((kind,data[cursor:cursor+length])); cursor+=length
    assert blocks[0][0]==0 and len(blocks[0][1])==34, 'Missing STREAMINFO'
    assert sum(kind==0 for kind,_ in blocks)==1, 'Duplicate STREAMINFO'
    stream=blocks[0][1]; packed=int.from_bytes(stream[10:18],'big')
    rate=packed>>44; channels=((packed>>41)&7)+1; bits=((packed>>36)&31)+1; samples=packed&0xfffffffff
    assert rate and samples, 'Unknown sample count'
    tags={}; pictures=[]
    for kind, payload in blocks:
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
        assert any(p['type']==3 for p in pictures), 'Missing embedded front cover'
    return {'path':str(path),'title':tags.get('TITLE'),'artist':tags.get('ARTIST'),'album':tags.get('ALBUM'),'album_artist':tags.get('ALBUMARTIST'),'track_number':int(tags.get('TRACKNUMBER','0')),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'duration_seconds':samples/rate,'sample_rate':rate,'channels':channels,'bits_per_sample':bits,'decoded_samples':samples,'decode_errors':0,'decoded_pcm_sha256':sha.hexdigest(),'streaminfo_md5_supplied':supplied_md5,'streaminfo_md5_verified':supplied_md5,'pictures':pictures,'embedded_tags':sorted(tags),'complete_listen':tags.get('HISTORY_COMPLETE_LISTEN')=='1'}


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('path',type=Path);parser.add_argument('--flac',default='flac');parser.add_argument('--required',type=int,default=1);parser.add_argument('--raw',action='store_true');parser.add_argument('--report',type=Path)
    args=parser.parse_args();files=[args.path] if args.path.is_file() else sorted(args.path.rglob('*.flac'))
    assert len(files)==args.required, f'Expected {args.required} files, found {len(files)}'
    rows=[validate(path,args.flac,args.raw) for path in files]
    report={'validator':'Xiph FLAC full decode, frame CRC and source STREAMINFO MD5 when supplied; no FFmpeg','sample_count':len(rows),'samples':rows}
    if args.report:args.report.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
