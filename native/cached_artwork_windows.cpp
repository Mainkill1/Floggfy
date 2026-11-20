#define WIN32_LEAN_AND_MEAN
#include "cached_artwork_windows.h"
#include "image_info.h"
#include <windows.h>
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace history {
namespace {
// Narrow read-only adapter for Chromium blockfile v3, not a cache implementation.
// Layout references: chromium/src/net/disk_cache/blockfile/{disk_format.h,
// disk_format_base.h,addr.h,storage_block-inl.h}. Unknown layouts fail closed.
constexpr size_t kImageLimit=4*1024*1024;
uint32_t U32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
uint32_t U16(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8);}
// Chromium PersistentHash is SuperFastHash. Algorithm copyright (c) 2010,
// Paul Hsieh. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither my name, Paul Hsieh, nor the names of any other contributors to the
//   code use may be used to endorse or promote products derived from this
//   software without specific prior written permission.
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
uint32_t Hash(const uint8_t* p,size_t n){
 uint32_t h=uint32_t(n);const auto rem=n&3;
 for(size_t i=0;i<n/4;++i,p+=4){h+=U16(p);const auto t=(U16(p+2)<<11)^h;h=(h<<16)^t;h+=h>>11;}
 if(rem==3){h+=U16(p);h^=h<<16;h^=uint32_t(int32_t(int8_t(p[2])))<<18;h+=h>>11;}
 else if(rem==2){h+=U16(p);h^=h<<11;h+=h>>17;}
 else if(rem==1){h+=uint32_t(int32_t(int8_t(*p)));h^=h<<10;h+=h>>1;}
 h^=h<<3;h+=h>>5;h^=h<<4;h+=h>>17;h^=h<<25;h+=h>>6;return h;
}
bool ValidUrl(const std::string& s){
 constexpr char prefix[]="https://i.scdn.co/image/";constexpr size_t n=sizeof(prefix)-1;
 return s.size()==n+40&&!s.compare(0,n,prefix)&&std::all_of(s.begin()+n,s.end(),[](char c){return(c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
struct File {
 HANDLE value=INVALID_HANDLE_VALUE;uint64_t size=0;
 explicit File(const std::filesystem::path& path){
  value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  LARGE_INTEGER n{};if(value!=INVALID_HANDLE_VALUE&&GetFileSizeEx(value,&n)&&n.QuadPart>=0)size=uint64_t(n.QuadPart);
 }
 ~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
 File(const File&)=delete;File& operator=(const File&)=delete;
 bool Read(uint64_t offset,void* dst,size_t count){
  if(value==INVALID_HANDLE_VALUE||offset>size||count>size-offset||count>kImageLimit)return false;
  LARGE_INTEGER pos{};pos.QuadPart=static_cast<LONGLONG>(offset);DWORD got=0;
  return SetFilePointerEx(value,pos,nullptr,FILE_BEGIN)&&ReadFile(value,dst,DWORD(count),&got,nullptr)&&got==count;
 }
};
bool Address(const std::filesystem::path& root,uint32_t addr,size_t size,std::vector<uint8_t>& out){
 if(!(addr&0x80000000u)||!size||size>kImageLimit)return false;
 const auto type=(addr>>28)&7;uint64_t offset=0;
 wchar_t name[32]{};
 if(type==0)std::swprintf(name,32,L"f_%06x",unsigned(addr&0x0fffffffu));
 else {
  if(type>4||(addr&0x0c000000u))return false;
  std::swprintf(name,32,L"data_%u",unsigned((addr>>16)&255));
 }
 File file(root/name);
 if(type){
  std::array<uint8_t,8192> header{};const uint32_t sizes[]={0,36,256,1024,4096};
  const auto block=sizes[type],start=addr&65535,count=((addr>>24)&3)+1;
  if(size>size_t(block)*count||!file.Read(0,header.data(),header.size())||
     U32(header.data())!=0xc104cac3u||U32(header.data()+4)!=0x20000||
     U16(header.data()+8)!=((addr>>16)&255)||U32(header.data()+12)!=block||
     U32(header.data()+56)!=0||U32(header.data()+20)>64896||start+count>U32(header.data()+20))return false;
  for(uint32_t i=start;i<start+count;++i)if(!(header[80+i/8]&(1u<<(i%8))))return false;
  offset=8192+uint64_t(start)*block;
 }else if(file.size!=size)return false;
 std::vector<uint8_t> data(size);if(!file.Read(offset,data.data(),size))return false;out=std::move(data);return true;
}
bool Checksum(const std::vector<uint8_t>& b,size_t end){return b.size()>=end+4&&U32(b.data()+end)==Hash(b.data(),end);}
template<class T> struct Com {
 T* p=nullptr;~Com(){if(p)p->Release();}T** Out(){return &p;}T* operator->()const{return p;}
};
bool Normalize(std::vector<uint8_t>& data,std::string& reason){
 ImageInfo info;if(ReadImageInfo(data,info))return true;
 if(data.size()<12||std::memcmp(data.data(),"RIFF",4)||std::memcmp(data.data()+8,"WEBP",4)||
    uint64_t(U32(data.data()+4))+8!=data.size()){reason="cache-image-invalid";return false;}
 // WIC is optional: hosts without a WebP decoder retain normal publication.
 // Both encoded streams have fixed 4 MiB capacities; dimensions bound decoding.
 std::vector<uint8_t> png(kImageLimit);
 {
 const auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 struct Apartment {HRESULT result;~Apartment(){if(SUCCEEDED(result))CoUninitialize();}} apartment{init};
 if(FAILED(init)&&init!=RPC_E_CHANGED_MODE){reason="cache-image-decoder-unavailable";return false;}
 Com<IWICImagingFactory> factory;Com<IWICStream> input,output;Com<IWICBitmapDecoder> decoder;
 Com<IWICBitmapFrameDecode> frame;Com<IWICBitmapEncoder> encoder;Com<IWICBitmapFrameEncode> target;
 Com<IWICFormatConverter> converter;
 if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(factory.Out())))||
    FAILED(factory->CreateStream(input.Out()))||FAILED(input->InitializeFromMemory(data.data(),DWORD(data.size())))||
    FAILED(factory->CreateDecoderFromStream(input.p,nullptr,WICDecodeMetadataCacheOnDemand,decoder.Out()))){reason="cache-image-decoder-unavailable";return false;}
 UINT width=0,height=0,frames=0;
 if(FAILED(decoder->GetFrameCount(&frames))||frames!=1||FAILED(decoder->GetFrame(0,frame.Out()))||
    FAILED(frame->GetSize(&width,&height))||!width||!height||width>8192||height>8192||uint64_t(width)*height>8000000){reason="cache-image-dimensions-invalid";return false;}
 WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
 if(FAILED(factory->CreateFormatConverter(converter.Out()))||
    FAILED(converter->Initialize(frame.p,format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))||
    FAILED(factory->CreateStream(output.Out()))||FAILED(output->InitializeFromMemory(png.data(),DWORD(png.size())))||
    FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.Out()))||FAILED(encoder->Initialize(output.p,WICBitmapEncoderNoCache))||
    FAILED(encoder->CreateNewFrame(target.Out(),nullptr))||FAILED(target->Initialize(nullptr))||
    FAILED(target->SetSize(width,height))||FAILED(target->SetPixelFormat(&format))||format!=GUID_WICPixelFormat32bppBGRA||
    FAILED(target->WriteSource(converter.p,nullptr))||FAILED(target->Commit())||FAILED(encoder->Commit())){reason="cache-image-conversion-failed";return false;}
 LARGE_INTEGER zero{};ULARGE_INTEGER end{};
 if(FAILED(output->Seek(zero,STREAM_SEEK_CUR,&end))||end.QuadPart>png.size()){reason="cache-image-conversion-failed";return false;}
 png.resize(size_t(end.QuadPart));
 if(!ReadImageInfo(png,info)||info.width!=width||info.height!=height){reason="cache-image-conversion-failed";return false;}
 } // Release every WIC object before replacing either stream's backing bytes.
 data=std::move(png);return true;
}
bool ReadDirectory(const std::filesystem::path& root,const std::string& url,std::vector<uint8_t>& bytes,std::string& reason){
 reason="cache-miss";if(!ValidUrl(url)){reason="invalid-url";return false;}
 File index(root/L"index");std::array<uint8_t,368> header{};
 if(!index.Read(0,header.data(),header.size())){reason="cache-unavailable";return false;}
 const auto table=U32(header.data()+28)?U32(header.data()+28):65536;
 if(U32(header.data())!=0xc103cac3u||U32(header.data()+4)!=0x30000||
    U32(header.data()+56)||table<1024||table>1048576||(table&(table-1))||index.size!=368+uint64_t(table)*4){reason="cache-layout-unsupported";return false;}
 // Exact observed Spotify top/frame site partition. Never match URL suffixes,
 // other origins, guessed resolutions, or unrelated artist/playlist artwork.
 const auto key=std::string("1/0/_dk_https://spotify.com https://spotify.com ")+url;
 const auto hash=Hash(reinterpret_cast<const uint8_t*>(key.data()),key.size());
 std::array<uint8_t,4> bucket{};const auto bucket_offset=368+uint64_t(hash&(table-1))*4;
 if(!index.Read(bucket_offset,bucket.data(),bucket.size()))return false;
 auto addr=U32(bucket.data());std::array<uint32_t,64> visited{};size_t visits=0;
 while(addr&&visits<visited.size()){
  if((addr&0xfc000000u)!=0xa0000000u||std::find(visited.begin(),visited.begin()+visits,addr)!=visited.begin()+visits){reason="cache-entry-invalid";return false;}
  visited[visits++]=addr;std::vector<uint8_t> entry;
  if(!Address(root,addr,256,entry)||!Checksum(entry,92)){reason="cache-entry-invalid";return false;}
  const auto p=entry.data();const auto next=U32(p+4),key_size=U32(p+32);
  if(U32(p)==hash&&key_size==key.size()&&!U32(p+36)&&key_size<160&&
     !std::memcmp(p+96,key.data(),key_size)&&p[96+key_size]==0){
   if(U32(p+20)||U32(p+72)||U32(p+48)||U32(p+52)){reason="cache-entry-invalid";return false;}
   const auto ranking=U32(p+8),length=U32(p+44),body=U32(p+60);
   std::vector<uint8_t> node,data,again,node_again;
   if((ranking&0xff000000u)!=0x90000000u||!Address(root,ranking,36,node)||
      !Checksum(node,32)||U32(node.data()+24)!=addr||U32(node.data()+28)){reason="cache-entry-busy";return false;}
   if(!length||length>kImageLimit||!Address(root,body,length,data)){reason="cache-body-unavailable";return false;}
   if(!Normalize(data,reason))return false;
   std::array<uint8_t,368> after{};std::array<uint8_t,4> bucket_after{};
   if(!Address(root,addr,256,again)||again!=entry||!Address(root,ranking,36,node_again)||node_again!=node||
      !index.Read(0,after.data(),after.size())||U32(after.data())!=U32(header.data())||
      U32(after.data()+4)!=U32(header.data()+4)||U32(after.data()+20)!=U32(header.data()+20)||
      U32(after.data()+28)!=U32(header.data()+28)||std::memcmp(after.data()+40,header.data()+40,8)||U32(after.data()+56)||
      !index.Read(bucket_offset,bucket_after.data(),bucket_after.size())||bucket_after!=bucket){reason="cache-entry-changed";return false;}
   bytes=std::move(data);reason="cache-hit";return true;
  }
  addr=next;
 }
 if(addr)reason="cache-chain-limit";
 return false;
}
} // namespace
bool ReadCachedArtwork(const std::string& url,std::vector<uint8_t>& bytes,std::string& reason){
 try{
  std::array<wchar_t,32768> local{};const auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),DWORD(local.size()));
  if(!n||n>=local.size()){reason="cache-directory-unavailable";return false;}
  return ReadDirectory(std::filesystem::path(local.data())/L"Spotify"/L"Browser"/L"Cache"/L"Cache_Data",url,bytes,reason);
 }catch(...){reason="cache-read-failed";return false;}
}
} // namespace history
