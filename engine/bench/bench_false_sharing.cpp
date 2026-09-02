#include <cstdio>
#include "rig/clock.hpp"
#include <cstdint>
#include <atomic>
#include <thread>
#include <vector>
#include <time.h>
static inline uint64_t now(){ return rig::mono_ns(); }
template<int STRIDE> void run(const char* label){
  static_assert(STRIDE>=8);
  alignas(256) static unsigned char buf[STRIDE*2 + 256];
  auto* a = new (buf) std::atomic<uint64_t>(0);
  auto* b = new (buf+STRIDE) std::atomic<uint64_t>(0);
  const int N=8000000;
  uint64_t t0=now();
  std::thread t1([&]{ for(int i=0;i<N;i++) a->fetch_add(1,std::memory_order_relaxed); });
  std::thread t2([&]{ for(int i=0;i<N;i++) b->fetch_add(1,std::memory_order_relaxed); });
  t1.join(); t2.join();
  uint64_t t3=now();
  printf("%-28s stride=%3d bytes  %7.2f ns/op\n", label, STRIDE, double(t3-t0)/(2.0*N));
}
int main(){
  run<8>("shared line (false sharing)");
  run<64>("64B separation");
  run<128>("128B separation");
  run<256>("256B separation");
  return 0;
}
