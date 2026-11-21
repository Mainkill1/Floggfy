#include "../tooling/binary.h"
#include "artwork_image.h"
#include "image_info.h"
#include "../vendor/libwebp/src/webp/decode.h"
#include <cstdlib>
#include <cstddef>
#include <limits>
#include <cstring>
#include <memory>
#include <new>
namespace {
#ifdef FLOGGFY_ARTWORK_ALLOC_TEST
size_t artwork_alloc_fail_after=std::numeric_limits<size_t>::max(),artwork_alloc_live=0;
#endif
// stb's stretchy buffers assume allocation succeeds. Throw before it can
// dereference/assert on null, and reclaim all of its C allocations on unwind.
struct alignas(std::max_align_t) PngBlock {PngBlock* previous;PngBlock* next;};
struct PngAllocations;
thread_local PngAllocations* png_allocations=nullptr;
struct PngAllocations {
 PngBlock* head=nullptr;PngAllocations* previous=png_allocations;
 PngAllocations(){png_allocations=this;}
 ~PngAllocations(){
  while(head){auto* next=head->next;std::free(head);head=next;
#ifdef FLOGGFY_ARTWORK_ALLOC_TEST
   --artwork_alloc_live;
#endif
  }
  png_allocations=previous;
 }
};
void CheckPngAllocation(size_t size){
 if(size>std::numeric_limits<size_t>::max()-sizeof(PngBlock))throw std::bad_alloc();
#ifdef FLOGGFY_ARTWORK_ALLOC_TEST
 if(!artwork_alloc_fail_after--)throw std::bad_alloc();
#endif
}
void* PngMalloc(size_t size){
 CheckPngAllocation(size);auto* p=static_cast<PngBlock*>(std::malloc(sizeof(PngBlock)+size));
 if(!p)throw std::bad_alloc();
 p->previous=nullptr;p->next=png_allocations->head;
 if(p->next)p->next->previous=p;
 png_allocations->head=p;
#ifdef FLOGGFY_ARTWORK_ALLOC_TEST
 ++artwork_alloc_live;
#endif
 return p+1;
}
void* PngRealloc(void* ptr,size_t size){
 if(!ptr)return PngMalloc(size);
 CheckPngAllocation(size);auto* old=static_cast<PngBlock*>(ptr)-1;
 auto* before=old->previous;auto* after=old->next;
 auto* p=static_cast<PngBlock*>(std::realloc(old,sizeof(PngBlock)+size));
 if(!p)throw std::bad_alloc();
 if(before)before->next=p;else png_allocations->head=p;
 if(after)after->previous=p;
 return p+1;
}
void PngFree(void* ptr){
 if(!ptr)return;
 auto* p=static_cast<PngBlock*>(ptr)-1;
 if(p->previous)p->previous->next=p->next;else png_allocations->head=p->next;
 if(p->next)p->next->previous=p->previous;
 std::free(p);
#ifdef FLOGGFY_ARTWORK_ALLOC_TEST
 --artwork_alloc_live;
#endif
}
}
#define STBIW_MALLOC(size) PngMalloc(size)
#define STBIW_REALLOC(ptr,size) PngRealloc(ptr,size)
#define STBIW_FREE(ptr) PngFree(ptr)
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
// The upstream header exposes unused private format writers and C initializers.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "../vendor/stb_image_write/stb_image_write.h"
#pragma GCC diagnostic pop
namespace history {
bool NormalizeArtworkImage(std::vector<uint8_t>& data,std::string& reason){
 constexpr size_t limit=4*1024*1024;
 ImageInfo info;if(ReadImageInfo(data,info))return true;
 if(data.size()<12||data.size()>limit||std::memcmp(data.data(),"RIFF",4)||std::memcmp(data.data()+8,"WEBP",4)){
  reason="cache-image-invalid";return false;
 }
 const uint32_t riff=binary::ReadLittle32(data.data()+4);
 WebPBitstreamFeatures features{};
 if(uint64_t(riff)+8!=data.size()||WebPGetFeatures(data.data(),data.size(),&features)!=VP8_STATUS_OK){reason="cache-image-invalid";return false;}
 if(features.has_animation){reason="cache-image-animation-unsupported";return false;}
 const auto w=features.width,h=features.height;
 if(w<=0||h<=0||w>8192||h>8192||uint64_t(w)*h>8000000){reason="cache-image-dimensions-invalid";return false;}
 try {
  // Dimensions cap RGBA at 32 MB and bound the writer's filtered/compressed
  // working buffers. Encoded input/output remain capped at 4 MiB.
  std::vector<uint8_t> rgba(size_t(w)*h*4);
  if(!WebPDecodeRGBAInto(data.data(),data.size(),rgba.data(),rgba.size(),w*4)){reason="cache-image-invalid";return false;}
  PngAllocations allocations;int length=0;
  std::unique_ptr<unsigned char,decltype(&PngFree)> png(stbi_write_png_to_mem(rgba.data(),w*4,w,h,4,&length),&PngFree);
  if(!png||length<=0||size_t(length)>limit){reason="cache-image-conversion-failed";return false;}
  std::vector<uint8_t> result(png.get(),png.get()+length);
  if(!ReadImageInfo(result,info)||info.width!=uint32_t(w)||info.height!=uint32_t(h)){reason="cache-image-conversion-failed";return false;}
  data=std::move(result);return true;
 }catch(const std::bad_alloc&){reason="cache-image-conversion-failed";return false;}
}
}
