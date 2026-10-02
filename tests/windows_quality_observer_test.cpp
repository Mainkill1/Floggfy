// Exercise real hook event handling with downloads off: no complete compressed
// buffers are retained, and original parser return values remain unchanged.
#include "../native/audio_history.cpp"
#include <cassert>
namespace history {
std::string ReadClientPlaybackQuality(const Media&) {return {};}
void EnrichTags(const Media &, Tags &) { assert(false && "observer test must not publish audio"); }
} // namespace history
static int parser_calls = 0;
static int32_t Parser(void *, OggPage *) {
  ++parser_calls;
  return 58;
}
int main() {
  assert(!GetSettings().downloads);
  queue =
      static_cast<Slot *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Slot) * capacity));
  assert(queue);
  event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  assert(event);
  quality_enabled = true;
  std::vector<uint8_t> data(70000);
  FlacEvent(reinterpret_cast<void *>(2), 3, data.data(), data.size());
  Slot first, second;
  assert(Pop(first) && Pop(second) && !Pop(first));
  assert(!first.capture && !second.capture && first.length == 42 && second.length == 42);
  assert(first.input_length + second.input_length == 70000);
  unsigned char header[28] = {}, body[30] = {};
  OggPage page{header, 28, body, 30};
  original = Parser;
  assert(Hook(reinterpret_cast<void *>(1), &page) == 58 && parser_calls == 1);
  Slot observed;
  assert(Pop(observed) && observed.kind == 0 && !observed.capture && observed.length == 58);
  quality_enabled = false;
  FlacEvent(reinterpret_cast<void *>(2), 3, data.data(), data.size());
  assert(Hook(reinterpret_cast<void *>(1), &page) == 58 && parser_calls == 2 && !Pop(observed));
  CloseHandle(event);
  event = nullptr;
  HeapFree(GetProcessHeap(), 0, queue);
  queue = nullptr;
  std::puts("PASS: downloads-off observation, bounded FLAC prefix copy, complete byte accounting "
            "and unchanged original decoder returns");
}
