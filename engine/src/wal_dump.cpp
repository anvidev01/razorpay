// wal-dump: decode the WAL to JSON lines, for the audit UI and for humans.
#include "rig/wal.hpp"
#include "rig/kernel.hpp"
#include <cstdio>
#include <cstring>
#include <string>

using namespace rig;

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "wal/rig.wal";
  std::uint64_t tail = 0;
  for (int i = 2; i < argc; ++i)
    if (std::string(argv[i]) == "--tail" && i + 1 < argc) tail = std::strtoull(argv[++i], nullptr, 10);

  std::vector<std::string> out;
  const ChainReport rep = wal_scan(path, [&](const WalRecord& r) {
    char buf[1024];
    const auto t = static_cast<RecType>(r.hdr.type);
    int k = std::snprintf(buf, sizeof buf,
      "{\"seq\":%llu,\"type\":\"%s\",\"wall_ns\":%llu,\"bytes\":%u,\"hash\":\"%s\"",
      (unsigned long long)r.hdr.seq, rectype_name(t),
      (unsigned long long)r.hdr.wall_ns, r.hdr.len,
      hex(r.this_hash).substr(0, 16).c_str());
    if (t == RecType::POLICY_DECISION && r.payload.size() == sizeof(DecisionPayload)) {
      DecisionPayload dp; std::memcpy(&dp, r.payload.data(), sizeof dp);
      k += std::snprintf(buf + k, sizeof buf - k,
        ",\"verdict\":%u,\"verdict_hex\":\"0x%04X\",\"total_paise\":%lld,\"lines\":%u,\"eval_ns\":%llu",
        dp.recorded_bits, dp.recorded_bits, (long long)dp.recorded_total,
        dp.n_lines, (unsigned long long)dp.eval_ns);
    }
    std::snprintf(buf + k, sizeof buf - k, "}");
    out.emplace_back(buf);
    return true;
  });

  const std::size_t start = (tail && out.size() > tail) ? out.size() - tail : 0;
  for (std::size_t i = start; i < out.size(); ++i) std::printf("%s\n", out[i].c_str());
  if (!rep.intact)
    std::fprintf(stderr, "CHAIN BROKEN at seq %llu: %s\n",
                 (unsigned long long)rep.break_seq, rep.detail.c_str());
  return rep.intact ? 0 : 1;
}
