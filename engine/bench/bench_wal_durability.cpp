#include <cstdio>
#include "rig/clock.hpp"
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <cstring>
#include <algorithm>
#include <vector>
static inline uint64_t now(){ return rig::mono_ns(); }
int main(){
  const char* path = "wal_probe.bin";
  int fd = ::open(path, O_CREAT|O_WRONLY|O_TRUNC, 0644);
  if (fd<0){ perror("open"); return 1; }
  char rec[256]; memset(rec,'A',sizeof rec);
  const int N = 300;
  auto bench=[&](const char* name, int mode){
    std::vector<uint64_t> s; s.reserve(N);
    for (int i=0;i<N;i++){
      uint64_t t0=now();
      ssize_t w = ::write(fd, rec, sizeof rec); (void)w;
      if (mode==1) ::fsync(fd);
      else if (mode==2) ::fcntl(fd, F_FULLFSYNC);
      s.push_back(now()-t0);
    }
    std::sort(s.begin(), s.end());
    printf("%-24s p50=%8.1f us  p99=%9.1f us  max=%9.1f us\n", name,
      s[N/2]/1000.0, s[(size_t)(N*0.99)]/1000.0, s.back()/1000.0);
  };
  bench("write only", 0);
  bench("write + fsync", 1);
  bench("write + F_FULLFSYNC", 2);
  ::close(fd); ::unlink(path);
  return 0;
}
