// Exercise the production scanner with virtual frames, without allocating a GiB.
#include "../native/mp3_stream.cpp"
#include <cassert>
#include <cstdio>
struct VirtualMp3 {
    size_t frames,frame_size;
    uint8_t bitrate;
    mutable size_t reads=0;
    size_t size()const{return frames*frame_size;}
    uint8_t operator[](size_t at)const {
        ++reads;
        switch(at%frame_size){case 0:return 0xff;case 1:return 0xfb;case 2:return bitrate;default:return 0;}
    }
};
int main() {
    history::Mp3Info info;std::string error;
    VirtualMp3 many{1000001,417,0x90};
    assert(history::ScanMp3(many,info,error,false)==history::Mp3Scan::Complete);
    assert(info.frames==1000001&&info.audio_size==many.size());
    VirtualMp3 large{(512u*1024u*1024u)/417+1,417,0x90};
    assert(history::ScanMp3(large,info,error,false)==history::Mp3Scan::Complete);
    assert(info.audio_size==large.size()&&large.size()>512u*1024u*1024u);
    VirtualMp3 maximum{(1024u*1024u*1024u)/417,417,0x90};
    assert(history::ScanMp3(maximum,info,error,false)==history::Mp3Scan::Complete);
    VirtualMp3 oversized{maximum.frames+1,417,0x90};
    assert(history::ScanMp3(oversized,info,error,false)==history::Mp3Scan::Invalid&&oversized.reads==0);
    VirtualMp3 too_long{3600000,208,0x50};
    assert(history::ScanMp3(too_long,info,error,false)==history::Mp3Scan::Invalid);
    puts("PASS: MP3 accepts large podcasts through 1 GiB, retains the size preflight and 24-hour duration bound");
}
