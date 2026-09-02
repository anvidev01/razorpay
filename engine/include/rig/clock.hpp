// Portable time and durable-flush primitives.
//
// The engine was written against Darwin extensions (clock_gettime_nsec_np,
// F_FULLFSYNC) and would not compile on Linux at all, despite run.sh advertising
// Debian packages. These wrappers keep the macOS behaviour exactly -- including the
// F_FULLFSYNC drive-cache flush that the durability numbers depend on -- and give
// Linux the closest equivalent.
#pragma once
#include <cstdint>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

namespace rig {

// Monotonic, unaffected by wall-clock adjustment. Used for every measurement.
inline std::uint64_t mono_ns() noexcept {
// RIG_FORCE_PORTABLE_CLOCK exercises the non-Darwin branch on a Mac, so the Linux
// path is compiled and RUN in CI here rather than first discovered on someone else's
// machine.
#if defined(__APPLE__) && !defined(RIG_FORCE_PORTABLE_CLOCK)
  return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
  struct timespec ts;
  ::clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<std::uint64_t>(ts.tv_nsec);
#endif
}

// Wall clock. Only for timestamps written into the audit log.
inline std::uint64_t wall_ns() noexcept {
  struct timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<std::uint64_t>(ts.tv_nsec);
}

// Flush all the way to durable media, not merely to the drive's write cache.
// On macOS plain fsync() does NOT flush that cache -- only F_FULLFSYNC does, which is
// why the WAL costs ~4ms per commit rather than ~30us. Getting this wrong would make
// the audit guarantee a fiction, so the fallback order matters.
inline int durable_flush(int fd) noexcept {
#if defined(F_FULLFSYNC) && !defined(RIG_FORCE_PORTABLE_CLOCK)
  if (::fcntl(fd, F_FULLFSYNC) == 0) return 0;
#endif
#if defined(__linux__)
  return ::fdatasync(fd);
#else
  return ::fsync(fd);
#endif
}

}  // namespace rig
