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
#include "rig/evidence.hpp"
#include "rig/wal.hpp"
#include "rig/kernel.hpp"
#include "rig/crypto.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace rig {

static std::string esc_ev(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (static_cast<unsigned char>(c) < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

EvidenceReport evidence_json(const std::string& wal, std::uint64_t want) {
  EvidenceReport out;
  std::string O;
  // Every call site below passes a string literal, but the compiler cannot see that
  // through the lambda, so -Wformat-security fires. Suppressed narrowly rather than
  // left to print a warning on every first build.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-security"
#endif
  const auto emit = [&O](const char* fmt, auto... a) {
    // Size the buffer from the formatted length. A fixed 4096 silently truncated the
    // raw cart on large baskets and produced MALFORMED JSON -- an evidence pack that
    // fails to parse is worse than none, because it fails precisely when the disputed
    // order was big. See docs/09-SECURITY-REVIEW.md.
    const int need = std::snprintf(nullptr, 0, fmt, a...);
    if (need <= 0) return;
    std::string tmp(static_cast<std::size_t>(need) + 1, '\0');
    std::snprintf(tmp.data(), tmp.size(), fmt, a...);
    O.append(tmp.c_str(), static_cast<std::size_t>(need));
  };
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

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

  if (!have_decision || decision.payload.size() != sizeof(DecisionPayload)) {
    out.json = "{\"error\":\"no POLICY_DECISION at that seq\"}";
    return out;
  }
  DecisionPayload dp;
  std::memcpy(&dp, decision.payload.data(), sizeof dp);

  // Re-execute the decision from the recorded inputs. An evidence pack that merely
  // quotes the log proves nothing; this proves the verdict follows from the inputs.
  const CartView cv{dp.sku_id, dp.unit_paise, dp.qty, dp.category_id,
                    dp.n_lines, dp.merchant_id, dp.text_flags, 0};
  const Verdict v = evaluate(dp.schema, cv, dp.now_ns);
  const bool reproduces = (v.bits == dp.recorded_bits && v.cart_total_paise == dp.recorded_total);

  emit("{\n");
  emit("  \"evidence_pack_version\": 1,\n");
  emit("  \"generated_for_dispute_on_decision_seq\": %llu,\n", (unsigned long long)want);
  emit("  \"log\": { \"path\": \"%s\", \"records\": %llu, \"chain_intact\": %s,\n",
              esc_ev(wal).c_str(), (unsigned long long)rep.records, rep.intact ? "true" : "false");
  emit("            \"chain_head\": \"%s\" },\n", hex(chain_head).c_str());

  emit("  \"1_human_authority\": {\n");
  emit("    \"question\": \"what did the human actually approve?\",\n");
  emit("    \"mandate_id\": %llu,\n", (unsigned long long)dp.schema.mandate_id);
  emit("    \"budget_paise\": %lld,\n", (long long)dp.schema.total_budget_paise);
  emit("    \"valid_from_ns\": %llu, \"valid_to_ns\": %llu,\n",
              (unsigned long long)dp.schema.not_before_ns,
              (unsigned long long)dp.schema.not_after_ns);
  emit("    \"approved_sku_count\": %u,\n", dp.schema.n_constraints);
  emit("    \"substitution_policy\": %u, \"substitution_max_delta_bp\": %u,\n",
              dp.schema.subst_policy, dp.schema.subst_max_delta_bp);
  emit("    \"ed25519_signed_at_admission\": %s,\n", have_mandate ? "true" : "false");
  if (have_mandate) {
    // Who signed it. This is the field a dispute actually turns on: the mandate was
    // signed by a key held on the user's device, which the gateway only verifies.
    std::string dev = "unknown";
    if (mandate.payload.size() >= 8 + 64 + 32)
      dev = hex(mandate.payload.data() + 8 + 64, 8);
    emit("    \"signed_by_device\": \"%s\",\n", dev.c_str());
    emit("    \"mandate_record_seq\": %llu, \"mandate_record_hash\": \"%s\"\n",
                (unsigned long long)mandate.hdr.seq, hex(mandate.this_hash).c_str());
  }
  else
    emit("    \"signed_by_device\": null, \"mandate_record_seq\": null\n");
  emit("  },\n");

  emit("  \"2_agent_proposal\": {\n");
  emit("    \"question\": \"what did the agent try to buy?\",\n");
  emit("    \"cart_lines\": %u, \"cart_total_paise\": %lld,\n",
              dp.n_lines, (long long)dp.recorded_total);
  emit("    \"injection_flags\": %u,\n", dp.text_flags);
  emit("    \"idempotency_key\": \"%s\",\n", hex(dp.idem_key, 32).c_str());
  if (have_cart) {
    std::string raw(reinterpret_cast<const char*>(cart.payload.data()), cart.payload.size());
    emit("    \"raw_cart_as_submitted\": \"%s\",\n", esc_ev(raw).c_str());
    emit("    \"cart_record_seq\": %llu\n", (unsigned long long)cart.hdr.seq);
  } else {
    emit("    \"raw_cart_as_submitted\": null\n");
  }
  emit("  },\n");

  emit("  \"3_policy_decision\": {\n");
  emit("    \"question\": \"what did the deterministic engine decide, and why?\",\n");
  emit("    \"outcome\": \"%s\",\n", dp.recorded_bits == R_NONE ? "ALLOW" : "DENY");
  emit("    \"verdict_bits\": %u, \"verdict_hex\": \"0x%04X\",\n",
              dp.recorded_bits, dp.recorded_bits);
  emit("    \"reasons\": [");
  bool first = true;
  for (int b = 0; b < R_BIT_COUNT; ++b) {
    const std::uint32_t bit = 1u << b;
    if (!(dp.recorded_bits & bit)) continue;
    if (!first) emit(", ");
    first = false;
    emit("{\"code\":\"%s\",\"detail\":\"%s\"}", reject_name(bit), reject_help(bit));
  }
  emit("],\n");
  emit("    \"decided_at_ns\": %llu, \"eval_ns\": %llu,\n",
              (unsigned long long)dp.now_ns, (unsigned long long)dp.eval_ns);
  emit("    \"engine\": \"rig deterministic kernel, schema v%u\"\n", dp.schema.schema_version);
  emit("  },\n");

  emit("  \"4_authorisation\": {\n");
  emit("    \"question\": \"was a payment capability actually issued?\",\n");
  if (have_cap) {
    const bool issued = static_cast<RecType>(capability.hdr.type) == RecType::CAPABILITY_ISSUED;
    emit("    \"capability\": \"%s\", \"record_seq\": %llu, \"record_hash\": \"%s\"\n",
                issued ? "ISSUED" : "DENIED",
                (unsigned long long)capability.hdr.seq, hex(capability.this_hash).c_str());
  } else {
    emit("    \"capability\": \"not found adjacent to this decision\"\n");
  }
  emit("  },\n");

  emit("  \"5_tamper_evidence\": {\n");
  emit("    \"question\": \"can this record have been altered after the fact?\",\n");
  emit("    \"decision_record_seq\": %llu,\n", (unsigned long long)decision.hdr.seq);
  emit("    \"prev_hash\": \"%s\",\n", hex(decision.prev_hash).c_str());
  emit("    \"this_hash\": \"%s\",\n", hex(decision.this_hash).c_str());
  emit("    \"note\": \"each record commits to its predecessor, so altering any "
              "earlier record invalidates every hash after it. Verify independently with: "
              "java -cp control-plane/out com.razorpay.rig.ReplayAuditor %s\"\n", esc_ev(wal).c_str());
  emit("  },\n");

  emit("  \"6_reproducibility\": {\n");
  emit("    \"question\": \"does the recorded verdict actually follow from the inputs?\",\n");
  emit("    \"re_executed\": true,\n");
  emit("    \"replay_verdict_hex\": \"0x%04X\", \"replay_total_paise\": %lld,\n",
              v.bits, (long long)v.cart_total_paise);
  emit("    \"matches_recorded\": %s\n", reproduces ? "true" : "false");
  emit("  }\n}\n");

  out.found        = true;
  out.reproduces   = reproduces;
  out.chain_intact = rep.intact;
  out.json         = O;
  return out;
}

}  // namespace rig

