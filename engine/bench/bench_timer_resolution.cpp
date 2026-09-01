#include <cstdio>
#include <cstdint>
#include <mach/mach_time.h>
#include <time.h>
#include <algorithm>
#include <vector>

int main(){
  mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  printf("mach timebase: %u/%u (ticks->ns multiplier %.6f)\n", tb.numer, tb.denom, (double)tb.numer/tb.denom);

  // smallest non-zero delta observable = effective resolution
  auto res_of = [](const char* name, auto fn){
    uint64_t best = UINT64_MAX;
    for (int i=0;i<200000;i++){ uint64_t a=fn(); uint64_t b=fn(); if (b>a) best=std::min(best,b-a); }
    printf("%-34s min observable delta: %llu\n", name, (unsigned long long)best);
  };
  res_of("mach_absolute_time()", []{ return mach_absolute_time(); });
  res_of("clock_gettime_nsec_np(UPTIME_RAW)", []{ return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); });
  res_of("clock_gettime_nsec_np(MONO_RAW)", []{ return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW); });

  // cost per call (batched)
  const int N = 2000000;
  uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  uint64_t sink=0; for (int i=0;i<N;i++) sink += mach_absolute_time();
  uint64_t t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  printf("mach_absolute_time cost: %.2f ns/call (sink=%llu)\n", double(t1-t0)/N, (unsigned long long)sink);

  t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  sink=0; for (int i=0;i<N;i++) sink += clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  t1 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  printf("clock_gettime_nsec_np cost: %.2f ns/call (sink=%llu)\n", double(t1-t0)/N, (unsigned long long)sink);
  return 0;
}
