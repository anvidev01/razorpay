#include <simdjson.h>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/qos.h>
using namespace simdjson;

static const char* CART = R"({"mandate_id":"mnd_8f21","merchant":"swiggy","lines":[
{"sku":"SKU_MEAL_THALI_001","unit_paise":24000,"qty":1},
{"sku":"SKU_DRINK_LIME_007","unit_paise":6000,"qty":2},
{"sku":"SKU_SIDE_RAITA_014","unit_paise":4500,"qty":1},
{"sku":"SKU_MEAL_BIRYANI_02","unit_paise":32000,"qty":1},
{"sku":"SKU_DESSERT_GULAB_9","unit_paise":8000,"qty":2},
{"sku":"SKU_DRINK_COLA_003","unit_paise":5000,"qty":1},
{"sku":"SKU_SIDE_PAPAD_021","unit_paise":2000,"qty":3},
{"sku":"SKU_MEAL_CURD_RICE1","unit_paise":18000,"qty":1}]})";

struct Out { uint32_t n; int64_t up[32]; uint32_t qty[32]; size_t skulen[32]; };

int main(){
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
  mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  const double t2ns=(double)tb.numer/tb.denom;
  printf("simdjson %s, active impl: %s\n", SIMDJSON_VERSION, get_active_implementation()->name().c_str());

  padded_string json = padded_string(CART, strlen(CART));
  ondemand::parser parser;
  if (parser.allocate(json.size(), 8)) { printf("allocate failed\n"); return 1; }

  Out o{};
  auto run=[&]() -> bool {
    auto doc = parser.iterate(json);
    o.n = 0;
    for (auto line : doc["lines"].get_array()) {
      std::string_view sv; if (line["sku"].get_string().get(sv)) return false;
      int64_t up; if (line["unit_paise"].get_int64().get(up)) return false;
      int64_t q;  if (line["qty"].get_int64().get(q)) return false;
      o.skulen[o.n]=sv.size(); o.up[o.n]=up; o.qty[o.n]=(uint32_t)q; ++o.n;
    }
    return true;
  };
  for(int i=0;i<50000;i++) if(!run()){ printf("parse err\n"); return 1; }
  printf("payload=%zu bytes, parsed lines=%u\n", json.size(), o.n);

  const int BATCH=200, ROUNDS=20000;
  std::vector<double> per; per.reserve(ROUNDS);
  for(int r=0;r<ROUNDS;r++){
    uint64_t t0=mach_absolute_time();
    for(int i=0;i<BATCH;i++){ run(); asm volatile("" :: "r"(&o) : "memory"); }
    uint64_t t1=mach_absolute_time();
    per.push_back(((t1-t0)*t2ns)/BATCH);
  }
  std::sort(per.begin(),per.end());
  printf("simdjson ondemand:       p50=%7.1f ns  p99=%7.1f ns  (%.2f GB/s)\n",
     per[ROUNDS/2], per[(size_t)(ROUNDS*0.99)], json.size()/per[ROUNDS/2]);
  return 0;
}
