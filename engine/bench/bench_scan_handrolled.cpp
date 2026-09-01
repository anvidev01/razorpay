// Hand-rolled fixed-schema scanner: parses a checkout cart into SoA + interned ids.
// No allocation, no std::string, no string_view escaping into downstream structures.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>

static const char* CART = R"({"mandate_id":"mnd_8f21","merchant":"swiggy","lines":[
{"sku":"SKU_MEAL_THALI_001","unit_paise":24000,"qty":1},
{"sku":"SKU_DRINK_LIME_007","unit_paise":6000,"qty":2},
{"sku":"SKU_SIDE_RAITA_014","unit_paise":4500,"qty":1},
{"sku":"SKU_MEAL_BIRYANI_02","unit_paise":32000,"qty":1},
{"sku":"SKU_DESSERT_GULAB_9","unit_paise":8000,"qty":2},
{"sku":"SKU_DRINK_COLA_003","unit_paise":5000,"qty":1},
{"sku":"SKU_SIDE_PAPAD_021","unit_paise":2000,"qty":3},
{"sku":"SKU_MEAL_CURD_RICE1","unit_paise":18000,"qty":1}]})";

// FNV-1a over the SKU bytes -> intern table probe (open addressing, power-of-two)
static constexpr uint32_t TSZ = 1024;
struct alignas(64) Slot { uint64_t h; uint32_t id; uint32_t _p; };
static Slot table[TSZ];
static uint32_t next_id = 1;

static inline uint64_t fnv(const char* p, size_t n){
  uint64_t h = 1469598103934665603ull;
  for (size_t i=0;i<n;i++){ h ^= (unsigned char)p[i]; h *= 1099511628211ull; }
  return h;
}
static inline uint32_t intern(const char* p, size_t n){
  uint64_t h = fnv(p,n); uint32_t i = (uint32_t)(h) & (TSZ-1);
  while (table[i].h && table[i].h != h) i = (i+1)&(TSZ-1);
  if (!table[i].h){ table[i].h=h; table[i].id=next_id++; }
  return table[i].id;
}

struct Out { uint32_t sku[32]; int64_t up[32]; uint32_t qty[32]; uint32_t n; };

// scan for a key then read its value; fixed schema so we can be blunt about it
static inline const char* find(const char* p, const char* end, const char* key, size_t klen){
  for (; p + klen < end; ++p) if (*p=='"' && !memcmp(p+1,key,klen)) return p+1+klen;
  return nullptr;
}
__attribute__((noinline))
static bool scan(const char* buf, size_t len, Out& o) noexcept {
  const char* p = buf; const char* end = buf+len;
  const char* lines = find(p,end,"lines\"",6); if(!lines) return false;
  o.n = 0; p = lines;
  while (p < end && o.n < 32) {
    const char* s = find(p,end,"sku\"",4); if(!s) break;
    s = (const char*)memchr(s,'"',end-s); if(!s) break; ++s;
    const char* e = (const char*)memchr(s,'"',end-s); if(!e) break;
    o.sku[o.n] = intern(s, (size_t)(e-s));
    const char* u = find(e,end,"unit_paise\"",11); if(!u) break;
    while (u<end && (*u<'0'||*u>'9')) ++u;
    int64_t v=0; while (u<end && *u>='0'&&*u<='9'){ v = v*10 + (*u-'0'); ++u; }
    o.up[o.n]=v;
    const char* q = find(u,end,"qty\"",4); if(!q) break;
    while (q<end && (*q<'0'||*q>'9')) ++q;
    uint32_t qq=0; while (q<end && *q>='0'&&*q<='9'){ qq = qq*10 + (uint32_t)(*q-'0'); ++q; }
    o.qty[o.n]=qq;
    ++o.n; p = q;
  }
  return o.n > 0;
}

int main(){
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
  mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  const double t2ns=(double)tb.numer/tb.denom;
  const size_t len = strlen(CART);
  Out o{};
  for(int i=0;i<50000;i++) scan(CART,len,o);
  printf("payload=%zu bytes, parsed lines=%u, first sku_id=%u\n", len, o.n, o.sku[0]);

  const int BATCH=200, ROUNDS=20000;
  std::vector<double> per; per.reserve(ROUNDS);
  for(int r=0;r<ROUNDS;r++){
    uint64_t t0=mach_absolute_time();
    for(int i=0;i<BATCH;i++){ scan(CART,len,o); asm volatile("" :: "r"(&o) : "memory"); }
    uint64_t t1=mach_absolute_time();
    per.push_back(((t1-t0)*t2ns)/BATCH);
  }
  std::sort(per.begin(),per.end());
  printf("hand-rolled scan+intern: p50=%7.1f ns  p99=%7.1f ns  (%.2f GB/s)\n",
     per[ROUNDS/2], per[(size_t)(ROUNDS*0.99)], len/per[ROUNDS/2]);
  return 0;
}
