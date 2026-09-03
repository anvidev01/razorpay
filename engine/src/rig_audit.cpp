// rig-audit: the dispute-grade audit trail, rendered so a judge can read it in 30s.
//
// rig-evidence emits machine-readable JSON for a dispute filing. This is the same
// record for a human: one transaction, in order, with the money action at the end.
#include "rig/wal.hpp"
#include "rig/kernel.hpp"
#include "rig/crypto.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <ctime>

using namespace rig;

static const char* G = "\033[32m"; static const char* R = "\033[31m";
static const char* Y = "\033[33m"; static const char* D = "\033[2m";
static const char* B = "\033[1m";  static const char* Z = "\033[0m";

static std::string hhmmss(std::uint64_t wall_ns) {
  const std::time_t s = static_cast<std::time_t>(wall_ns / 1000000000ull);
  std::tm tmv{}; localtime_r(&s, &tmv);
  char buf[16]; std::strftime(buf, sizeof buf, "%H:%M:%S", &tmv);
  return buf;
}

static int run(int argc, char** argv) {
  const std::string wal = argc > 1 ? argv[1] : "wal/rig.wal";
  const std::uint64_t only = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 0;

  std::printf("\n  %sAUDIT TRAIL%s  %s%s%s\n", B, Z, D, wal.c_str(), Z);
  std::printf("  %severy money action, in order, with the reason it was allowed or refused%s\n\n",
              D, Z);
  std::printf("  %s%-4s %-9s %-22s %s%s\n", D, "seq", "time", "event", "detail", Z);
  std::printf("  %s%s%s\n", D, "---- --------- ---------------------- "
              "--------------------------------------------", Z);

  std::uint64_t decisions = 0, allowed = 0, review = 0, denied = 0, paid = 0, dupes = 0;

  const ChainReport rep = wal_scan(wal, [&](const WalRecord& r) {
    const auto t = static_cast<RecType>(r.hdr.type);
    if (only && r.hdr.seq != only && t == RecType::POLICY_DECISION) return true;

    const char* col = D;
    std::string detail;
    char buf[512];

    switch (t) {
      case RecType::MANDATE_ISSUED: {
        col = G;
        detail = "human signed an intent mandate (Ed25519)";
        break;
      }
      case RecType::CART_PROPOSED: {
        detail = "agent proposed a cart (" + std::to_string(r.payload.size()) + " bytes)";
        break;
      }
      case RecType::POLICY_DECISION: {
        if (r.payload.size() != sizeof(DecisionPayload)) { detail = "malformed"; break; }
        DecisionPayload dp; std::memcpy(&dp, r.payload.data(), sizeof dp);
        ++decisions;
        const auto oc = static_cast<Outcome>(dp.outcome);
        col = oc == Outcome::ALLOW ? G : (oc == Outcome::REVIEW ? Y : R);
        if (oc == Outcome::ALLOW) ++allowed; else if (oc == Outcome::REVIEW) ++review; else ++denied;
        std::snprintf(buf, sizeof buf, "%-6s 0x%04X  Rs %.2f  %u lines  %lluns",
          outcome_name(oc), dp.recorded_bits, dp.recorded_total / 100.0,
          dp.n_lines, (unsigned long long)dp.eval_ns);
        detail = buf;
        std::printf("  %s%-4llu %-9s %s%-22s %s%s%s\n", D, (unsigned long long)r.hdr.seq,
          hhmmss(r.hdr.wall_ns).c_str(), col, rectype_name(t), detail.c_str(), Z, "");
        for (int b = 0; b < static_cast<int>(R_BIT_COUNT); ++b) {
          const std::uint32_t bit = 1u << b;
          if (!(dp.recorded_bits & bit)) continue;
          std::printf("  %s     %-9s %-22s  |- %-24s %s%s\n", D, "", "",
                      reject_name(bit), reject_help(bit), Z);
        }
        return true;
      }
      case RecType::CAPABILITY_ISSUED: col = G; detail = "payment token minted (single use, cart-bound)"; break;
      case RecType::CAPABILITY_DENIED: col = R; detail = "no token -- cannot reach the rail"; break;
      case RecType::STEP_UP_REQUIRED:  col = Y; detail = "escalated to the human (not blocked)"; break;
      case RecType::HUMAN_CONFIRMED: {
        struct HC { std::uint64_t id, at; std::uint8_t approved; char ref[32]; std::uint8_t cart[32]; };
        if (r.payload.size() >= sizeof(HC)) {
          HC hc; std::memcpy(&hc, r.payload.data(), sizeof hc);
          col = hc.approved ? G : R;
          std::snprintf(buf, sizeof buf, "human %s (ref %s) bound to decision #%llu",
            hc.approved ? "APPROVED" : "DECLINED", hc.ref, (unsigned long long)hc.id);
          detail = buf;
        }
        break;
      }
      case RecType::PAYMENT_ATTEMPTED: detail = "submitting to the payment rail"; break;
      case RecType::PAYMENT_RESULT: {
        struct PR { std::uint64_t id; std::uint8_t ok; long status; char order[40]; char err[80]; };
        if (r.payload.size() >= sizeof(PR)) {
          PR pr; std::memcpy(&pr, r.payload.data(), sizeof pr);
          col = pr.ok ? G : R;
          if (pr.ok) ++paid;
          std::snprintf(buf, sizeof buf, "%s  http %ld  %s",
            pr.ok ? "PAID" : "FAILED", pr.status,
            pr.ok ? pr.order : (pr.err[0] ? pr.err : "no reason recorded"));
          detail = buf;
        }
        break;
      }
      case RecType::DUPLICATE_SUPPRESSED: {
        col = Y; ++dupes;
        detail = "agent retry collapsed onto the original decision -- no second charge";
        break;
      }
      case RecType::REVERSAL_REQUESTED: {
        struct Req { std::uint64_t rid, of; std::int64_t amt; std::uint32_t bits; char why[64]; };
        if (r.payload.size() >= sizeof(Req)) {
          Req q; std::memcpy(&q, r.payload.data(), sizeof q);
          col = q.bits ? R : Y;
          std::string codes;
          for (int b = 0; b < static_cast<int>(R_BIT_COUNT); ++b) {
            const std::uint32_t bit = 1u << b;
            if (!(q.bits & bit)) continue;
            if (!codes.empty()) codes += ", ";
            codes += reject_name(bit);
          }
          std::snprintf(buf, sizeof buf, "refund Rs %.2f of decision #%llu -- %s%s%s",
            q.amt / 100.0, (unsigned long long)q.of,
            q.bits ? "REFUSED: " : "authorised", q.bits ? codes.c_str() : "",
            q.why[0] && !q.bits ? "" : "");
          detail = buf;
        }
        break;
      }
      case RecType::REVERSAL_RESULT: {
        struct Res { std::uint64_t of; std::int64_t amt; std::uint8_t ok; long status;
                     char ref[40]; char err[96]; };
        if (r.payload.size() >= sizeof(Res)) {
          Res s2; std::memcpy(&s2, r.payload.data(), sizeof s2);
          col = s2.ok ? G : R;
          const bool no_payment = std::strstr(s2.err, "not a valid id") != nullptr;
          std::snprintf(buf, sizeof buf, "%s  http %ld  %s",
            s2.ok ? "REFUNDED" : (no_payment ? "rail declined" : "refund failed"),
            s2.status,
            s2.ok ? s2.ref
                  : (no_payment ? "no payment exists against this order to reverse"
                                : (s2.err[0] ? s2.err : "no reason recorded")));
          detail = buf;
        }
        break;
      }
      case RecType::REMEDIATION: detail = "remediation recorded"; break;
      default: detail = ""; break;
    }
    std::printf("  %s%-4llu %-9s %s%-22s %s%s\n", D, (unsigned long long)r.hdr.seq,
      hhmmss(r.hdr.wall_ns).c_str(), col, rectype_name(t), detail.c_str(), Z);
    return true;
  });

  std::printf("\n  %ssummary%s  %llu decisions: %s%llu allowed%s, %s%llu review%s, %s%llu denied%s"
              "  |  %llu paid, %llu retries collapsed\n",
    B, Z, (unsigned long long)decisions, G, (unsigned long long)allowed, Z,
    Y, (unsigned long long)review, Z, R, (unsigned long long)denied, Z,
    (unsigned long long)paid, (unsigned long long)dupes);
  if (rep.not_found) {
    // Distinct exit code (2) so a mistyped path is never mistaken for tampering.
    std::printf("  %schain%s    %sno such log%s -- %s\n", B, Z, Y, Z, rep.detail.c_str());
    std::printf("  %sthe log is missing, not corrupt. check the path, or run "
                "./scripts/seed.sh then make a decision first.%s\n\n", D, Z);
    return 2;
  }
  std::printf("  %schain%s    %llu records, SHA-256 %s%s%s%s\n\n", B, Z,
    (unsigned long long)rep.records, rep.intact ? G : R,
    rep.intact ? "INTACT -- no record altered since it was written" : "BROKEN", Z,
    rep.intact ? "" : "");
  return rep.intact ? 0 : 1;
}

int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& e) { std::fprintf(stderr, "\n  error: %s\n\n", e.what()); return 4; }
}
