// rig-evidence: assembles a dispute evidence pack for one decision.
//
// THE PROBLEM (confirmed by the chargeback industry, not hypothesised): when an agent
// buys, the evidence a merchant would normally use to defend a chargeback disappears.
// No human click trail, no cardholder session, and the device fingerprint belongs to
// the agent's host rather than the buyer. Today the merchant absorbs the loss by
// default, because nothing in the record distinguishes an honest agent mistake from
// genuine fraud -- or from a customer who authorised the purchase and changed their mind.
//
// This tool emits exactly that missing record: what the human authorised, what the
// agent proposed, what the deterministic policy decided and why, and where all of it
// sits in a tamper-evident log that a third party can re-verify offline.
#include "rig/wal.hpp"
#include "rig/kernel.hpp"
#include "rig/crypto.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace rig;

static std::string esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

static int run(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: rig-evidence <wal> <decision_seq>\n");
    return 2;
  }
  const std::string wal = argv[1];
  const std::uint64_t want = std::strtoull(argv[2], nullptr, 10);

  WalRecord mandate{}, cart{}, decision{}, capability{};
  bool have_mandate = false, have_cart = false, have_decision = false, have_cap = false;
  Hash256 chain_head{};
  std::uint64_t total_records = 0;

  const ChainReport rep = wal_scan(wal, [&](const WalRecord& r) {
    const auto t = static_cast<RecType>(r.hdr.type);
    total_records = r.hdr.seq;
    chain_head    = r.this_hash;
    if (t == RecType::MANDATE_ISSUED && r.hdr.seq < want) { mandate = r; have_mandate = true; }
    if (t == RecType::CART_PROPOSED  && r.hdr.seq < want) { cart = r;    have_cart = true; }
    if (t == RecType::POLICY_DECISION && r.hdr.seq == want) { decision = r; have_decision = true; }
    if ((t == RecType::CAPABILITY_ISSUED || t == RecType::CAPABILITY_DENIED)
        && r.hdr.seq == want + 1) { capability = r; have_cap = true; }
    return true;
  });

  if (!have_decision) {
    std::fprintf(stderr, "no POLICY_DECISION at seq %llu in %s\n",
                 (unsigned long long)want, wal.c_str());
    return 3;
  }
  if (decision.payload.size() != sizeof(DecisionPayload)) {
    std::fprintf(stderr, "decision payload size mismatch\n");
    return 3;
  }
  DecisionPayload dp;
  std::memcpy(&dp, decision.payload.data(), sizeof dp);

  // Re-execute the decision from the recorded inputs. An evidence pack that merely
  // quotes the log proves nothing; this proves the verdict follows from the inputs.
  const CartView cv{dp.sku_id, dp.unit_paise, dp.qty, dp.category_id,
                    dp.n_lines, dp.merchant_id, dp.text_flags, 0};
  const Verdict v = evaluate(dp.schema, cv, dp.now_ns);
  const bool reproduces = (v.bits == dp.recorded_bits && v.cart_total_paise == dp.recorded_total);

  std::printf("{\n");
  std::printf("  \"evidence_pack_version\": 1,\n");
  std::printf("  \"generated_for_dispute_on_decision_seq\": %llu,\n", (unsigned long long)want);
  std::printf("  \"log\": { \"path\": \"%s\", \"records\": %llu, \"chain_intact\": %s,\n",
              esc(wal).c_str(), (unsigned long long)rep.records, rep.intact ? "true" : "false");
  std::printf("            \"chain_head\": \"%s\" },\n", hex(chain_head).c_str());

  std::printf("  \"1_human_authority\": {\n");
  std::printf("    \"question\": \"what did the human actually approve?\",\n");
  std::printf("    \"mandate_id\": %llu,\n", (unsigned long long)dp.schema.mandate_id);
  std::printf("    \"budget_paise\": %lld,\n", (long long)dp.schema.total_budget_paise);
  std::printf("    \"valid_from_ns\": %llu, \"valid_to_ns\": %llu,\n",
              (unsigned long long)dp.schema.not_before_ns,
              (unsigned long long)dp.schema.not_after_ns);
  std::printf("    \"approved_sku_count\": %u,\n", dp.schema.n_constraints);
  std::printf("    \"substitution_policy\": %u, \"substitution_max_delta_bp\": %u,\n",
              dp.schema.subst_policy, dp.schema.subst_max_delta_bp);
  std::printf("    \"ed25519_signed_at_admission\": %s,\n", have_mandate ? "true" : "false");
  if (have_mandate)
    std::printf("    \"mandate_record_seq\": %llu, \"mandate_record_hash\": \"%s\"\n",
                (unsigned long long)mandate.hdr.seq, hex(mandate.this_hash).c_str());
  else
    std::printf("    \"mandate_record_seq\": null\n");
  std::printf("  },\n");

  std::printf("  \"2_agent_proposal\": {\n");
  std::printf("    \"question\": \"what did the agent try to buy?\",\n");
  std::printf("    \"cart_lines\": %u, \"cart_total_paise\": %lld,\n",
              dp.n_lines, (long long)dp.recorded_total);
  std::printf("    \"injection_flags\": %u,\n", dp.text_flags);
  std::printf("    \"idempotency_key\": \"%s\",\n", hex(dp.idem_key, 32).c_str());
  if (have_cart) {
    std::string raw(reinterpret_cast<const char*>(cart.payload.data()), cart.payload.size());
    std::printf("    \"raw_cart_as_submitted\": \"%s\",\n", esc(raw).c_str());
    std::printf("    \"cart_record_seq\": %llu\n", (unsigned long long)cart.hdr.seq);
  } else {
    std::printf("    \"raw_cart_as_submitted\": null\n");
  }
  std::printf("  },\n");

  std::printf("  \"3_policy_decision\": {\n");
  std::printf("    \"question\": \"what did the deterministic engine decide, and why?\",\n");
  std::printf("    \"outcome\": \"%s\",\n", dp.recorded_bits == R_NONE ? "ALLOW" : "DENY");
  std::printf("    \"verdict_bits\": %u, \"verdict_hex\": \"0x%04X\",\n",
              dp.recorded_bits, dp.recorded_bits);
  std::printf("    \"reasons\": [");
  bool first = true;
  for (int b = 0; b < R_BIT_COUNT; ++b) {
    const std::uint32_t bit = 1u << b;
    if (!(dp.recorded_bits & bit)) continue;
    if (!first) std::printf(", ");
    first = false;
    std::printf("{\"code\":\"%s\",\"detail\":\"%s\"}", reject_name(bit), reject_help(bit));
  }
  std::printf("],\n");
  std::printf("    \"decided_at_ns\": %llu, \"eval_ns\": %llu,\n",
              (unsigned long long)dp.now_ns, (unsigned long long)dp.eval_ns);
  std::printf("    \"engine\": \"rig deterministic kernel, schema v%u\"\n", dp.schema.schema_version);
  std::printf("  },\n");

  std::printf("  \"4_authorisation\": {\n");
  std::printf("    \"question\": \"was a payment capability actually issued?\",\n");
  if (have_cap) {
    const bool issued = static_cast<RecType>(capability.hdr.type) == RecType::CAPABILITY_ISSUED;
    std::printf("    \"capability\": \"%s\", \"record_seq\": %llu, \"record_hash\": \"%s\"\n",
                issued ? "ISSUED" : "DENIED",
                (unsigned long long)capability.hdr.seq, hex(capability.this_hash).c_str());
  } else {
    std::printf("    \"capability\": \"not found adjacent to this decision\"\n");
  }
  std::printf("  },\n");

  std::printf("  \"5_tamper_evidence\": {\n");
  std::printf("    \"question\": \"can this record have been altered after the fact?\",\n");
  std::printf("    \"decision_record_seq\": %llu,\n", (unsigned long long)decision.hdr.seq);
  std::printf("    \"prev_hash\": \"%s\",\n", hex(decision.prev_hash).c_str());
  std::printf("    \"this_hash\": \"%s\",\n", hex(decision.this_hash).c_str());
  std::printf("    \"note\": \"each record commits to its predecessor, so altering any "
              "earlier record invalidates every hash after it. Verify independently with: "
              "java -cp control-plane/out com.razorpay.rig.ReplayAuditor %s\"\n", esc(wal).c_str());
  std::printf("  },\n");

  std::printf("  \"6_reproducibility\": {\n");
  std::printf("    \"question\": \"does the recorded verdict actually follow from the inputs?\",\n");
  std::printf("    \"re_executed\": true,\n");
  std::printf("    \"replay_verdict_hex\": \"0x%04X\", \"replay_total_paise\": %lld,\n",
              v.bits, (long long)v.cart_total_paise);
  std::printf("    \"matches_recorded\": %s\n", reproduces ? "true" : "false");
  std::printf("  }\n}\n");

  return reproduces && rep.intact ? 0 : 1;
}

int main(int argc, char** argv) {
  try { return run(argc, argv); }
  catch (const std::exception& e) { std::fprintf(stderr, "\n  error: %s\n\n", e.what()); return 4; }
}
