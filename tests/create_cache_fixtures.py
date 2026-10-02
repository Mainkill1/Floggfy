"""Synthetic LevelDB/protobuf metadata. Contains no client or listening data."""
from pathlib import Path
import struct,sys
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def vi(n):
 b=bytearray()
 while n>=128:b.append((n&127)|128);n>>=7
 b.append(n);return bytes(b)
def v(k,n):return vi(k<<3)+vi(n)
def blob(k,b):
 if isinstance(b,str):b=b.encode()
 return vi(k<<3|2)+vi(len(b))+b
artist=lambda name:blob(2,name)
album=blob(1,bytes(15)+b'\x02')+blob(2,'Album')+blob(3,artist('Lead'))+blob(3,artist('Guest'))+blob(5,'Example Records')+blob(6,v(1,4048)+v(2,10)+v(3,12))+blob(13,v(1,1)+blob(2,'2024 Example Publishing'))
track=blob(1,bytes(15)+b'\x01')+blob(2,'Song')+blob(3,album)+blob(4,artist('Lead'))+blob(4,artist('Guest'))+v(5,2)+v(6,2)+v(7,400000)+blob(10,blob(1,'isrc')+blob(2,'XXABC2400001'))
uri='spotify:track:0000000000000000000001'
key=b'!xmeta#cache#\x01*#$'+uri.encode()+b'#'
value=v(1,10)+blob(2,blob(1,'type.googleapis.com/spotify.metadata.Track')+blob(2,track))
def crc(b):
 c=0xffffffff
 for n in b:
  c^=n
  for _ in range(8):c=(c>>1)^(0x82f63b78 if c&1 else 0)
 return c^0xffffffff
def masked(b):
 c=crc(b);return ((c>>15)|(c<<17)&0xffffffff)+0xa282ead8&0xffffffff
def block(entries):
 b=b''.join(vi(0)+vi(len(k))+vi(len(val))+k+val for k,val in entries)
 return b+struct.pack('<II',0,1)
def raw_snappy(b):
 n=len(b)-1
 if n<60:tag=bytes([n<<2])
 else:
  size=(n.bit_length()+7)//8;tag=bytes([(59+size)<<2])+n.to_bytes(size,'little')
 return vi(len(b))+tag+b
internal=key+struct.pack('<Q',100<<8|1)
def table(compressed=False):
 data=block([(internal,value)]);raw=raw_snappy(data) if compressed else data;kind=bytes([int(compressed)])
 buf=raw+kind+struct.pack('<I',masked(raw+kind));idx=block([(internal,vi(0)+vi(len(raw)))])
 index_handle=vi(len(buf))+vi(len(idx));buf+=idx+b'\0'+struct.pack('<I',masked(idx+b'\0'))
 footer=(vi(0)+vi(0)+index_handle).ljust(40,b'\0')+struct.pack('<Q',0xdb4775248b80fb57)
 return buf+footer
(out/'table.ldb').write_bytes(table())
(out/'snappy.ldb').write_bytes(table(True))
bad=bytearray(table());bad[40]^=1;(out/'corrupt.ldb').write_bytes(bad)
def wal_record(payload,kind):
 return struct.pack('<IHB',masked(bytes([kind])+payload),len(payload),kind)+payload
batch=struct.pack('<QI',101,1)+b'\x01'+vi(len(key))+key+vi(len(value))+value
(out/'complete.log').write_bytes(wal_record(batch,1))
(out/'fragmented.log').write_bytes(wal_record(batch[:100],2)+wal_record(batch[100:],4))
(out/'truncated.log').write_bytes(wal_record(batch,1)+wal_record(batch,1)[:18])
deleted=struct.pack('<QI',102,1)+b'\0'+vi(len(key))+key
(out/'deleted.log').write_bytes(wal_record(deleted,1))
def pack_table(blocks,repeat=False):
 buf=b'';handles=[]
 for data in blocks:
  raw=raw_snappy(data);handle=vi(len(buf))+vi(len(raw));handles.append(handle)
  buf+=raw+b'\x01'+struct.pack('<I',masked(raw+b'\x01'))
 if repeat:handles=handles*2
 idx=block([(internal,h) for h in handles]);ih=vi(len(buf))+vi(len(idx))
 return buf+idx+b'\0'+struct.pack('<I',masked(idx+b'\0'))+(vi(0)+vi(0)+ih).ljust(40,b'\0')+struct.pack('<Q',0xdb4775248b80fb57)
(out/'repeated-handle.ldb').write_bytes(pack_table([block([(internal,value)])],True))
mini=blob(1,bytes(15)+b'\x02')+blob(2,'Album')+blob(3,artist('Lead'))+blob(6,v(1,4048))
partial_track=blob(1,bytes(15)+b'\x01')+blob(2,'Song')+blob(3,mini)+blob(4,artist('Lead'))+blob(4,artist('Guest'))+v(7,400000)
partial_value=v(1,10)+blob(2,blob(1,'type.googleapis.com/spotify.metadata.Track')+blob(2,partial_track))
album_uri='spotify:album:0000000000000000000002'
album_key=b'!xmeta#cache#\x01)#$'+album_uri.encode()+b'#'
album_value=v(1,10)+blob(2,blob(1,'type.googleapis.com/spotify.metadata.Album')+blob(2,album))
(out/'separate-album.ldb').write_bytes(pack_table([block([(album_key+struct.pack('<Q',100<<8|1),album_value),(internal,partial_value)])]))
# A wrong embedded GID must not be accepted even if the cache key matches.
wrong_track=track.replace(bytes(15)+b'\x01',bytes(15)+b'\x03',1)
wrong_value=v(1,10)+blob(2,blob(1,'type.googleapis.com/spotify.metadata.Track')+blob(2,wrong_track))
(out/'wrong-gid.ldb').write_bytes(pack_table([block([(internal,wrong_value)])]))
# Five distinct blocks can expand beyond the aggregate limit without repeated
# handles. Valid copy tags keep the physical file small and exercise Snappy.
noise_key=b'noise'+struct.pack('<Q',100<<8|1)
zeros=7*1024*1024
head=vi(0)+vi(len(noise_key))+vi(zeros)+noise_key+b'\0'
tail=struct.pack('<II',0,1)
raw=vi(len(head)+zeros-1+len(tail))+raw_snappy(head)[len(vi(len(head))):]
copies,left=divmod(zeros-1,64)
raw+=b'\xfe\x01\0'*copies
if left:raw+=bytes([((left-1)<<2)|2])+b'\x01\0'
raw+=raw_snappy(tail)[len(vi(len(tail))):]
buf=b'';handles=[]
for _ in range(5):
 handles.append(vi(len(buf))+vi(len(raw)));buf+=raw+b'\x01'+struct.pack('<I',masked(raw+b'\x01'))
idx=block([(internal,h) for h in handles]);ih=vi(len(buf))+vi(len(idx))
(out/'expansion-limit.ldb').write_bytes(buf+idx+b'\0'+struct.pack('<I',masked(idx+b'\0'))+(vi(0)+vi(0)+ih).ljust(40,b'\0')+struct.pack('<Q',0xdb4775248b80fb57))
