// 2AM bug, faithfully: a LARGE cart (8 lines) is parsed; the audit ring retains a view
// into a high arena offset. The NEXT request is a SMALL cart (1 line), so that offset is
// no longer in use -- it is reset memory. The WAL serializer then reads the stale view.
#include <cstdio>
#include <cstring>
#include <string_view>
#include <cstdlib>
#if defined(__has_feature)
# if __has_feature(address_sanitizer)
#  include <sanitizer/asan_interface.h>
# endif
#endif
#ifndef ASAN_POISON_MEMORY_REGION
# define ASAN_POISON_MEMORY_REGION(a,b)   ((void)(a),(void)(b))
# define ASAN_UNPOISON_MEMORY_REGION(a,b) ((void)(a),(void)(b))
#endif

struct Arena {
  char* base; size_t cap, head = 0;
  explicit Arena(size_t c) : base((char*)malloc(c)), cap(c) {
#if POISON
    ASAN_POISON_MEMORY_REGION(base, cap);
#endif
  }
  char* alloc(size_t n){
    if (head + n > cap) return nullptr;
    char* p = base + head; head += n;
#if POISON
    ASAN_UNPOISON_MEMORY_REGION(p, n);
#endif
    return p;
  }
  void reset(){
#if POISON
    ASAN_POISON_MEMORY_REGION(base, head);
#endif
    head = 0;
  }
};

static std::string_view g_ring[8];
static int g_n = 0;

static void handle_request(Arena& a, int nlines){
  a.reset();
  for (int i = 0; i < nlines; ++i){
    char* p = a.alloc(32);
    memcpy(p, "SKU_MEAL_THALI_0000000000000", 28);
    g_ring[g_n++ % 8] = std::string_view(p, 28);   // BUG: retains borrowed arena memory
  }
}

int main(){
  Arena a(4096);
  handle_request(a, 8);   // big cart  -> ring holds views up to offset 224
  handle_request(a, 1);   // small cart-> only offset 0..32 is live again
  size_t total = 0;
  for (int i = 0; i < 8; ++i)
    if (!g_ring[i].empty()) total += (unsigned char)g_ring[i][0];  // <-- reads stale bytes
  printf("serialized checksum %zu (POISON=%d)\n", total, POISON);
  return 0;
}
