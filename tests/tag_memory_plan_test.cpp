#include "../native/tag_memory_plan.h"
#include "recording_fixture.h"
#include <cassert>
#include <cstdio>
int main() {
 using namespace history;
 const size_t source=180u*1024*1024,limit=512u*1024*1024;
 Tags tags;tags.fields={{"TITLE","Large podcast"},{"LYRICS","bounded lyrics"}};
 for(const auto codec:{Codec::Flac,Codec::Vorbis,Codec::Mp3}) {
  TagMemoryPlan plan;assert(PlanTagConstructionMemory(codec,source,42,65536,tags,plan));
  assert(source+plan.incremental_peak<limit&&"180 MiB source plus replacement and scratch fit the unchanged pool");
  assert(plan.output_capacity>source&&plan.incremental_peak>=plan.output_capacity);
 }
 TagMemoryPlan plan;const auto flac=recording_fixture::fixture();
 assert(PlanTagConstructionMemory(Codec::Flac,flac.size(),42,0,tags,plan)&&plan.output_capacity>flac.size());
 assert(!PlanTagConstructionMemory(Codec::Flac,SIZE_MAX,42,0,tags,plan));
 assert(!PlanTagConstructionMemory(Codec::Vorbis,source,0,SIZE_MAX,tags,plan));
 assert(!PlanTagConstructionMemory(Codec::Flac,source,SIZE_MAX,0,tags,plan));
 size_t packet=0;const auto ogg=recording_fixture::ogg();assert(LargestOggPacket(ogg.data(),ogg.size(),packet)&&packet>0);
 assert(!LargestOggPacket(ogg.data(),ogg.size()-1,packet));
 std::puts("PASS: allocation-free tag construction bounds preserve large-source headroom and reject overflow/truncation");
}
