// rig-replay: the offline auditor.
//
// Because evaluate() is a pure function of its recorded inputs, every historical
// decision can be re-executed and checked against what was logged. This turns
// "explainable" from a claim into a verifiable property.
#include "rig/wal.hpp"
#include "rig/kernel.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rig;

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "wal/rig.wal";
  const bool json  = (argc > 2 && std::string(argv[2]) == "--json");

  std::uint64_t decisions = 0, divergent = 0, allows = 0, reviews = 0, denies = 0, stateful = 0;
  std::uint64_t first_divergent_seq = 0;
  std::vector<std::string> details;

  const ChainReport rep = wal_scan(path, [&](const WalRecord& r) {
    if (static_cast<RecType>(r.hdr.type) != RecType::POLICY_DECISION) return true;
    if (r.payload.size() != sizeof(DecisionPayload)) {
      ++divergent;
      if (!first_divergent_seq) first_divergent_seq = r.hdr.seq;
      return true;
    }
    DecisionPayload dp;
    std::memcpy(&dp, r.payload.data(), sizeof dp);
    ++decisions;

    const CartView c{dp.sku_id, dp.unit_paise, dp.qty, dp.category_id, dp.recurring_paise,
                     dp.n_lines, dp.merchant_id, dp.text_flags, 0};
    const Verdict  v = evaluate(dp.schema, c, dp.now_ns);   // re-execute, same inputs

    // Exclude bits that came from cross-transaction state; the kernel cannot
    // reproduce those from a single record, and counting them would be a false
    // divergence. They are still recorded, and still shown below.
    const std::uint32_t reproducible = dp.recorded_bits & ~STATEFUL_RISK_MASK;
    if (dp.recorded_bits & STATEFUL_RISK_MASK) ++stateful;
    if (v.bits != reproducible || v.cart_total_paise != dp.recorded_total) {
      ++divergent;
      if (!first_divergent_seq) first_divergent_seq = r.hdr.seq;
      char buf[256];
      std::snprintf(buf, sizeof buf,
        "seq %llu: recorded 0x%04X (reproducible 0x%04X)/%lld, replay 0x%04X/%lld",
        (unsigned long long)r.hdr.seq, dp.recorded_bits, reproducible,
        (long long)dp.recorded_total, v.bits, (long long)v.cart_total_paise);
      details.emplace_back(buf);
    }
    // Count by the RECORDED outcome, not by kernel bits: a REVIEW has no hard bits
    // but is not an allow. Reporting it as allowed would contradict the audit log.
    switch (static_cast<Outcome>(dp.outcome)) {
      case Outcome::ALLOW:  ++allows;  break;
      case Outcome::REVIEW: ++reviews; break;
      default:              ++denies;  break;
    }
    return true;
  });

  if (json) {
    std::printf("{\"records\":%llu,\"chain_intact\":%s,\"decisions\":%llu,"
                "\"allowed\":%llu,\"denied\":%llu,\"divergent\":%llu,\"detail\":\"%s\"}\n",
      (unsigned long long)rep.records, rep.intact ? "true" : "false",
      (unsigned long long)decisions, (unsigned long long)allows,
      (unsigned long long)denies, (unsigned long long)divergent, rep.detail.c_str());
    return (rep.intact && divergent == 0) ? 0 : 1;
  }

  const char* G = "\033[32m"; const char* R = "\033[31m"; const char* D = "\033[2m"; const char* Z = "\033[0m";
  const char* Y = "\033[33m";
  std::printf("\n  %saudit replay%s  %s\n", D, Z, path);
  if (rep.not_found) {
    std::printf("  chain     : %sno such log%s -- %s\n", Y, Z, rep.detail.c_str());
    std::printf("  %sthe log is missing, not corrupt. check the path, or run "
                "./scripts/seed.sh then make a decision first.%s\n\n", D, Z);
    return 2;
  }
  std::printf("  chain     : %llu records, SHA-256 chain %s%s%s\n",
    (unsigned long long)rep.records, rep.intact ? G : R,
    rep.intact ? "INTACT" : "BROKEN", Z);
  if (!rep.intact)
    std::printf("  %s          -> %s at seq %llu%s\n", R, rep.detail.c_str(),
                (unsigned long long)rep.break_seq, Z);
  std::printf("  replay    : %llu decisions re-executed against recorded inputs\n",
              (unsigned long long)decisions);
  std::printf("  outcome   : %llu allowed, %llu review, %llu denied\n",
              (unsigned long long)allows, (unsigned long long)reviews,
              (unsigned long long)denies);
  std::printf("  divergent : %s%llu%s\n", divergent ? R : G,
              (unsigned long long)divergent, Z);
  if (stateful)
    std::printf("  %snote      : %llu decision(s) also carried behavioural risk bits, which "
                "derive from\n              cross-transaction state and are recorded but not "
                "kernel-reproducible%s\n", D, (unsigned long long)stateful, Z);
  for (const auto& s : details) std::printf("    %s%s%s\n", R, s.c_str(), Z);
  if (rep.intact && divergent == 0 && decisions)
    std::printf("\n  %sOK%s every money action in this log is reproducible from its inputs\n\n", G, Z);
  else
    std::printf("\n  %sFAIL%s this log does not verify\n\n", R, Z);
  return (rep.intact && divergent == 0) ? 0 : 1;
}
