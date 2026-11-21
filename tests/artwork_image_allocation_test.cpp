// Exercise every PNG-writer allocation failure through the real converter.
#define FLOGGFY_ARTWORK_ALLOC_TEST
#include "../native/media/artwork_image.cpp"
#include "artwork_webp_fixture.h"
#include <cassert>
#include <iostream>
int main(){
 const auto original=artwork_fixture::webp_alpha();
 size_t failed=0;
 for(size_t i=0;i<1000;++i){
  artwork_alloc_fail_after=i;
  auto image=original;std::string reason;
  bool ok=history::NormalizeArtworkImage(image,reason);
  assert(artwork_alloc_live==0);
  if(ok){assert(failed>5);std::cout<<"PASS: "<<failed<<" PNG-writer allocation failures preserve input and release every allocation\n";return 0;}
  assert(image==original&&reason=="cache-image-conversion-failed");++failed;
 }
 assert(false);
}
