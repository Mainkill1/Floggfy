// Standalone test includes the adapter to exercise a private synthetic directory.
#include "../native/cached_artwork_windows.cpp"
#include "artwork_fixture.h"
#include "../native/image_info.h"
#include <cassert>
#include <fstream>
#include <iostream>
using Bytes=std::vector<uint8_t>;
static void Set(Bytes& b,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)b[at+i]=uint8_t(v>>(i*8));}
static void Save(const std::filesystem::path& path,const Bytes& b){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());assert(f.good());}
int main(int argc,char** argv){
 assert(argc==2);const auto root=std::filesystem::path(argv[1]);std::filesystem::create_directories(root);
 const auto url=std::string("https://i.scdn.co/image/")+std::string(40,'a');
 const auto key=std::string("1/0/_dk_https://spotify.com https://spotify.com ")+url;
 const auto hash=history::Hash(reinterpret_cast<const uint8_t*>(key.data()),key.size());
 const uint32_t addr=0xa0010000,ranking=0x90000000,body=0x80000001;
 auto png=artwork_fixture::png();Bytes index(368+1024*4),entry(256),node(36),blocks(8192+256),ranks(8192+36);
 Set(index,0,0xc103cac3);Set(index,4,0x30000);Set(index,28,1024);Set(index,368+(hash&1023)*4,addr);
 Set(entry,0,hash);Set(entry,8,ranking);Set(entry,32,uint32_t(key.size()));Set(entry,44,uint32_t(png.size()));Set(entry,60,body);
 std::memcpy(entry.data()+96,key.data(),key.size());
 Set(node,24,addr);
 auto seal=[&]{Set(entry,92,history::Hash(entry.data(),92));Set(node,32,history::Hash(node.data(),32));};
 for(auto* b:{&blocks,&ranks}){Set(*b,0,0xc104cac3);Set(*b,4,0x20000);Set(*b,20,1);(*b)[80]=1;}
 Set(blocks,8,1);Set(blocks,12,256);Set(ranks,12,36);
 auto save=[&]{seal();std::copy(entry.begin(),entry.end(),blocks.begin()+8192);std::copy(node.begin(),node.end(),ranks.begin()+8192);Save(root/L"index",index);Save(root/L"data_1",blocks);Save(root/L"data_0",ranks);Save(root/L"f_000001",png);};
 save();Bytes out{7,8,9};std::string reason;
 // A concurrently open writer is allowed, with no cache engine or file locks.
 HANDLE writer=CreateFileW((root/L"data_1").c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
 assert(writer!=INVALID_HANDLE_VALUE);assert(history::ReadDirectory(root,url,out,reason)&&out==png&&reason=="cache-hit");CloseHandle(writer);
 const Bytes sentinel{7,8,9};auto fail=[&](const std::string& u=std::string{}){out=sentinel;assert(!history::ReadDirectory(root,u.empty()?url:u,out,reason)&&out==sentinel);};
 fail("https://i.scdn.co/image/"+std::string(40,'b'));fail(url+"?extra");
 Set(node,28,1);save();fail();Set(node,28,0);
 Set(entry,20,2);save();fail();Set(entry,20,0);
 Set(entry,44,4*1024*1024+1);save();fail();Set(entry,44,uint32_t(png.size()));
 save();blocks[8192+92]^=1;Save(root/L"data_1",blocks);fail();
 save();blocks[80]=0;Save(root/L"data_1",blocks);fail();blocks[80]=1;
 Set(entry,0,hash+1024);Set(entry,4,addr);save();fail();Set(entry,0,hash);Set(entry,4,0);
 save();Set(index,4,0x40000);Save(root/L"index",index);fail();Set(index,4,0x30000);
 save();png[0]^=1;Save(root/L"f_000001",png);fail();png[0]^=1;
 // Also exercise an internal body address and its allocation/header checks.
 Set(entry,60,0xa0010001);blocks.resize(8192+512);Set(blocks,20,2);blocks[80]=3;
 save();std::copy(png.begin(),png.end(),blocks.begin()+8192+256);Save(root/L"data_1",blocks);
 assert(history::ReadDirectory(root,url,out,reason)&&out==png);
 Bytes webp={0x52,0x49,0x46,0x46,0x22,0,0,0,0x57,0x45,0x42,0x50,0x56,0x50,0x38,0x20,0x16,0,0,0,0x30,1,0,0x9d,1,0x2a,1,0,1,0,0x0e,0xc0,0xfe,0x25,0xa4,0,3,0x70,0,0,0,0};
 const bool converted=history::NormalizeArtworkImage(webp,reason);assert(converted);
 history::ImageInfo info;assert(history::ReadImageInfo(webp,info)&&info.mime=="image/png"&&info.width==1&&info.height==1);
 std::cout<<"PASS: exact partition key, external/internal bodies, shared reads, corruption, dirty entry, bounds, unsupported layout; WebP conversion="<<converted<<"\n";
}
