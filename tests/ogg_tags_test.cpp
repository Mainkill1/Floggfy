#include "../native/ogg_tags.h"
#include <ogg/ogg.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <cstdlib>
static void check(bool b,const char* name) { if(!b) { std::fprintf(stderr,"FAIL: %s\n",name); std::exit(1); } }
static std::vector<uint8_t> fixture() {
    ogg_stream_state stream; ogg_stream_init(&stream,42);
    std::vector<uint8_t> input; ogg_page page;
    unsigned char id[30]={}; std::memcpy(id,"\x01vorbis",7); id[11]=2; id[12]=0x44; id[13]=0xac;
    unsigned char comment[]={3,'v','o','r','b','i','s',0,0,0,0,0,0,0,0,1};
    unsigned char setup[]={5,'v','o','r','b','i','s'};
    unsigned char audio[]={0,1,2,3,4,5};
    unsigned char* packets[]={id,comment,setup,audio}; long lengths[]={30,16,7,6};
    for(int i=0;i<4;i++) {
        ogg_packet p={packets[i],lengths[i],i==0,i==3,i==3?44100:0,i};
        ogg_stream_packetin(&stream,&p);
        while(ogg_stream_flush(&stream,&page)) {
            input.insert(input.end(),page.header,page.header+page.header_len);
            input.insert(input.end(),page.body,page.body+page.body_len);
        }
    }
    ogg_stream_clear(&stream); return input;
}
int main(int argc,char** argv) {
    auto source=fixture(); history::Tags tags;
    tags.fields={{"TITLE","A title"},{"ARTIST","An artist"},{"HISTORY_COMPLETE_LISTEN","1"}};
    tags.mime="image/png"; tags.cover.resize(100000,'x'); std::memcpy(tags.cover.data(),"\x89PNG\r\n\x1a\n",8);
    std::vector<uint8_t> output; std::string error;
    check(history::TagOgg(source,tags,output,error),"native tagger accepts complete stream");
    check(output.size()>source.size()+100000,"embedded picture spans multiple Ogg pages");
    ogg_sync_state sync; ogg_sync_init(&sync);
    std::memcpy(ogg_sync_buffer(&sync,long(output.size())),output.data(),output.size());
    ogg_sync_wrote(&sync,long(output.size())); ogg_stream_state stream; ogg_stream_init(&stream,42);
    ogg_page page; ogg_packet packet; bool title=false,picture=false; int packets=0; long seq=0;
    while(ogg_sync_pageout(&sync,&page)==1) {
        check(ogg_page_pageno(&page)==seq++,"tagged Ogg sequences contiguous");
        check(ogg_stream_pagein(&stream,&page)==0,"tagged pages accepted");
        while(ogg_stream_packetout(&stream,&packet)==1) {
            if(packets==1) {
                std::string s(reinterpret_cast<char*>(packet.packet),packet.bytes);
                title=s.find("TITLE=A title")!=std::string::npos;
                picture=s.find("METADATA_BLOCK_PICTURE=")!=std::string::npos;
            }
            if(packets==3) check(packet.bytes==6 && packet.packet[5]==5 && packet.e_o_s && packet.granulepos==44100,
                               "compressed audio packet and EOS granule unchanged");
            ++packets;
        }
    }
    check(packets==4 && title && picture,"embedded tags and artwork recoverable");
    ogg_stream_clear(&stream); ogg_sync_clear(&sync);
    auto corrupt=source; corrupt.back()^=1;
    check(!history::TagOgg(corrupt,tags,output,error),"damaged source cannot be tagged");
    if(argc==4) {
        std::ifstream in(argv[1],std::ios::binary),image(argv[2],std::ios::binary);
        source.assign(std::istreambuf_iterator<char>(in),{});
        tags.cover.assign(std::istreambuf_iterator<char>(image),{});
        check(history::TagOgg(source,tags,output,error),"actual complete capture remuxes");
        std::ofstream out(argv[3],std::ios::binary); out.write(reinterpret_cast<char*>(output.data()),output.size());
        check(bool(out),"actual tagged capture written");
    }
    std::puts("PASS: embedded Ogg tags, large picture, packet preservation and corruption rejection");
}
