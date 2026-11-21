#include "../native/media/artwork_image.h"
#include "../native/media/image_info.h"
#include "artwork_fixture.h"
#include "artwork_webp_fixture.h"
#include <cassert>
#include <fstream>
#include <iostream>
using Bytes=std::vector<uint8_t>;
static void Set(Bytes& b,size_t pos,uint32_t n){for(unsigned i=0;i<4;++i)b[pos+i]=uint8_t(n>>(8*i));}
int main(int argc,char** argv){
 std::string reason;
 for(auto original:{artwork_fixture::png(),artwork_fixture::jpeg()}){auto data=original;assert(history::NormalizeArtworkImage(data,reason)&&data==original);}
 for(auto data:{artwork_fixture::webp_lossy(),artwork_fixture::webp_alpha()}){assert(history::NormalizeArtworkImage(data,reason));history::ImageInfo info;assert(history::ReadImageInfo(data,info)&&info.mime=="image/png");}
 auto fails=[&](Bytes data,const char* why){auto original=data;assert(!history::NormalizeArtworkImage(data,reason)&&data==original);assert(reason==why);};
 fails({},"cache-image-invalid");fails(Bytes(4*1024*1024+1,0),"cache-image-invalid");
 auto bad=artwork_fixture::webp_lossy();bad.pop_back();fails(bad,"cache-image-invalid");
 bad.resize(31);Set(bad,4,uint32_t(bad.size()-8));fails(bad,"cache-image-invalid");
 // Extended WebP header reports animation before decoding any image data.
 Bytes animation={'R','I','F','F',22,0,0,0,'W','E','B','P','V','P','8','X',10,0,0,0,2,0,0,0,0,0,0,0,0,0};
 fails(animation,"cache-image-animation-unsupported");
 auto large=artwork_fixture::webp_lossy();large[26]=1;large[27]=32;fails(large,"cache-image-dimensions-invalid");
 large=artwork_fixture::webp_lossy();large[26]=0xb8;large[27]=0x0b;large[28]=0xb8;large[29]=0x0b;fails(large,"cache-image-dimensions-invalid");
 if(argc==2){auto alpha=artwork_fixture::webp_alpha();assert(history::NormalizeArtworkImage(alpha,reason));std::ofstream f(argv[1],std::ios::binary);f.write(reinterpret_cast<const char*>(alpha.data()),alpha.size());assert(f.good());}
 std::cout<<"PASS: JPEG/PNG pass-through, lossy/lossless WebP, alpha PNG, malformed/truncated/animated images and byte/dimension/pixel limits\n";
}
