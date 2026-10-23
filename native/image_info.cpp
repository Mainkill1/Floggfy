#include "image_info.h"
#include <cstring>
namespace history {
static uint32_t Big(const uint8_t* p){return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
static bool Bounds(const ImageInfo& i){return i.width && i.height && i.width<=8192 && i.height<=8192 && uint64_t(i.width)*i.height<=32000000;}
bool ReadImageInfo(const std::vector<uint8_t>& b,ImageInfo& out){
    out={};if(b.size()<8 || b.size()>4*1024*1024)return false;
    ImageInfo i;
    if(!std::memcmp(b.data(),"\x89PNG\r\n\x1a\n",8)){
        bool header=false,data=false,palette=false;size_t pos=8;
        while(b.size()-pos>=12){
            uint32_t n=Big(b.data()+pos);if(n>b.size()-pos-12)return false;
            const uint8_t* type=b.data()+pos+4;const uint8_t* p=type+4;
            uint32_t crc=~0u;for(size_t k=pos+4;k<pos+8+n;++k){crc^=b[k];for(int bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0);}
            if(~crc!=Big(p+n))return false;
            if(!header){
                if(std::memcmp(type,"IHDR",4)||n!=13)return false;
                i.width=Big(p);i.height=Big(p+4);unsigned depth=p[8],color=p[9],channels=0;
                if(color==0 && (depth==1||depth==2||depth==4||depth==8||depth==16))channels=1;
                if(color==2 && (depth==8||depth==16))channels=3;
                if(color==3 && (depth==1||depth==2||depth==4||depth==8))channels=1;
                if(color==4 && (depth==8||depth==16))channels=2;
                if(color==6 && (depth==8||depth==16))channels=4;
                if(!channels||p[10]||p[11]||p[12]>1||!Bounds(i))return false;
                i.depth=depth*channels;i.mime="image/png";header=true;
            }else if(!std::memcmp(type,"IHDR",4))return false;
            else if(!std::memcmp(type,"PLTE",4)){
                if(data||!n||n%3||n>768)return false;
                i.colors=n/3;palette=true;
            }else if(!std::memcmp(type,"IDAT",4)){
                if(!n)return false;
                data=true;
            }else if(!std::memcmp(type,"IEND",4)){
                if(n||!data||pos+12!=b.size())return false;
                if(b[25]==3 && !palette)return false;
                if(b[25]!=3)i.colors=0;
                out=i;return true;
            }else if(!(type[0]&32))return false;
            pos+=12+n;
        }return false;
    }
    if(b[0]!=0xff||b[1]!=0xd8)return false;
    size_t pos=2;bool frame=false,scan=false;
    while(pos<b.size()){
        if(b[pos++]!=0xff)return false;
        while(pos<b.size()&&b[pos]==0xff)++pos;
        if(pos==b.size())return false;
        unsigned marker=b[pos++];
        if(marker==0xd9){if(!frame||!scan||pos!=b.size())return false;i.mime="image/jpeg";out=i;return true;}
        if(marker==0||marker==0xd8||marker==1||(marker>=0xd0&&marker<=0xd7))return false;
        if(b.size()-pos<2)return false;
        size_t n=(size_t(b[pos])<<8)|b[pos+1];if(n<2||n>b.size()-pos)return false;
        if(marker==0xc0||marker==0xc1||marker==0xc2){
            if(frame||n<8)return false;
            unsigned channels=b[pos+7];if((channels!=1&&channels!=3&&channels!=4)||n!=8+3*channels||b[pos+2]!=8)return false;
            i.height=(uint32_t(b[pos+3])<<8)|b[pos+4];i.width=(uint32_t(b[pos+5])<<8)|b[pos+6];i.depth=8*channels;
            if(!Bounds(i))return false;
            frame=true;
        }
        if(marker==0xda){
            if(!frame||n<6||n!=size_t(6+2*b[pos+2]))return false;
            pos+=n;size_t start=pos;
            while(pos<b.size()){
                if(b[pos]!=0xff){++pos;continue;}
                if(pos+1>=b.size())return false;
                if(b[pos+1]==0 || (b[pos+1]>=0xd0&&b[pos+1]<=0xd7)){pos+=2;continue;}
                break;
            }
            if(pos==start)return false;
            scan=true;
        }else pos+=n;
    }return false;
}
}
