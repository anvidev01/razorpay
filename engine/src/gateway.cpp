#include "rig/gateway.hpp"
#include <time.h>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <vector>
#include <algorithm>

namespace rig {

static std::uint64_t mono_ns() { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

Gateway::Gateway(std::string wal_path)
    : wal_(std::make_unique<Wal>(std::move(wal_path))) {
  for (int i = 0; i < 16; ++i) tag_key_[i] = static_cast<std::uint8_t>(0xA5 ^ (i * 31));
}

Tag128 Gateway::tag_of(const IntentSchema& s) const noexcept {
  IntentSchema copy = s;
  std::memset(copy.integrity_tag, 0, sizeof copy.integrity_tag);   // tag excludes itself
  return siphash(&copy, sizeof copy, tag_key_);
}

bool Gateway::admit_mandate(const std::string& intent_json, std::string& err) {
  IntentSchema s{};
  const ParseResult pr = parse_intent(intent_json, intern_, s);
  if (!pr.ok) { err = pr.error; return false; }

  // Ed25519 costs 36.5us -- 1302x the kernel. It is paid ONCE, here, never per cart.
  const Sig512 sig = key_.sign(&s, sizeof(IntentSchema) - sizeof(s.integrity_tag));
  if (!key_.verify(&s, sizeof(IntentSchema) - sizeof(s.integrity_tag), sig)) {
    err = "mandate signature verification failed";
    return false;
  }
  const Tag128 tag = tag_of(s);
  std::memcpy(s.integrity_tag, tag.data(), 16);

  std::uint32_t idx;
  auto it = by_id_.find(s.mandate_id);
  if (it != by_id_.end()) { idx = it->second; }
  else {
    idx = pool_.acquire();
    if (idx == decltype(pool_)::SENTINEL) { err = "mandate pool exhausted"; return false; }
    by_id_[s.mandate_id] = idx;
  }
  pool_.at(idx) = s;

  struct { std::uint64_t id; std::uint8_t sig[64]; } rec{};
  rec.id = s.mandate_id;
  std::memcpy(rec.sig, sig.data(), 64);
  wal_->append(RecType::MANDATE_ISSUED, &rec, sizeof rec);
  wal_->commit();
  return true;
}

Decision Gateway::decide(const std::string& cart_json, std::uint64_t now_ns) {
  Decision d;
  d.decision_id = next_decision_++;

  arena_.reset();
  CartView cart{};
  std::uint64_t mandate_id = 0;
  const ParseResult pr = parse_cart(cart_json, intern_, arena_, cart, d.cart_hash, mandate_id);
  d.mandate_id = mandate_id;

  if (!pr.ok) {
    d.parsed = false;
    d.parse_error = pr.error;
    d.verdict.bits = pr.bits ? pr.bits : R_SCHEMA_VERSION;
    // A cart we cannot parse is a cart we cannot approve. Fail closed, and still log it.
    wal_->append(RecType::CART_PROPOSED, cart_json.data(), cart_json.size());
    d.wal_seq   = wal_->next_seq();
    d.commit_us = wal_->commit();
    wal_->append(RecType::CAPABILITY_DENIED, &d.verdict.bits, sizeof d.verdict.bits);
    wal_->commit();
    return d;
  }
  d.parsed = true;

  auto it = by_id_.find(mandate_id);
  if (it == by_id_.end()) {
    d.verdict.bits = R_MANDATE_EXPIRED;      // no such mandate -> nothing authorises this
    d.parse_error  = "unknown mandate";
    wal_->append(RecType::CAPABILITY_DENIED, &d.verdict.bits, sizeof d.verdict.bits);
    wal_->commit();
    return d;
  }
  IntentSchema& s = pool_.at(it->second);

  // In-memory integrity: prove the cached schema was not mutated since admission.
  // ~50ns, versus 36500ns to re-verify the Ed25519 signature.
  const Tag128 want = tag_of(s);
  if (std::memcmp(want.data(), s.integrity_tag, 16) != 0) {
    d.verdict.bits = R_ENGINE_RESOURCE;
    d.parse_error  = "mandate integrity tag mismatch -- evicted";
    by_id_.erase(it);
    wal_->append(RecType::CAPABILITY_DENIED, &d.verdict.bits, sizeof d.verdict.bits);
    wal_->commit();
    return d;
  }

  const std::uint64_t t0 = mono_ns();
  d.verdict = evaluate(s, cart, now_ns);
  d.eval_ns = mono_ns() - t0;

  // snapshot inputs so the CLI can report a batched, quantisation-free kernel time
  last_schema_   = s;
  last_n_        = cart.n > MAX_CART ? MAX_CART : cart.n;
  last_merchant_ = cart.merchant_id;
  last_now_      = now_ns;
  for (std::uint32_t i = 0; i < last_n_; ++i) {
    last_sku_[i] = cart.sku_id[i]; last_up_[i] = cart.unit_paise[i]; last_qty_[i] = cart.qty[i];
  }
  d.amount_paise = d.verdict.cart_total_paise;

  // --- record the full inputs so the auditor can re-execute this decision offline ---
  DecisionPayload dp{};
  dp.schema      = s;
  dp.now_ns      = now_ns;
  dp.merchant_id = cart.merchant_id;
  dp.n_lines     = cart.n;
  for (std::uint32_t i = 0; i < cart.n && i < MAX_CART; ++i) {
    dp.sku_id[i]     = cart.sku_id[i];
    dp.unit_paise[i] = cart.unit_paise[i];
    dp.qty[i]        = cart.qty[i];
  }
  dp.recorded_bits  = d.verdict.bits;
  dp.recorded_total = d.verdict.cart_total_paise;
  dp.eval_ns        = d.eval_ns;
  std::memcpy(&dp.cart_hash_lo, d.cart_hash.data(), 8);

  wal_->append(RecType::CART_PROPOSED, cart_json.data(), cart_json.size());
  d.wal_seq = wal_->append(RecType::POLICY_DECISION, &dp, sizeof dp);

  // THE FENCE. Nothing below this line may run before the record is durable.
  // Under group commit the fence is the BATCH boundary, not every record -- but a
  // capability token is still never minted before its own record is on stable storage.
  const Hash256 rec_head = wal_->head();
  if (group_commit_) {
    wal_->maybe_commit();
    if (wal_->committed_seq() < d.wal_seq) { d.commit_us = 0; return d; }  // pending
  } else {
    d.commit_us = wal_->commit();
  }

  if (d.verdict.allowed()) {
    PctBody b{};
    b.decision_id  = d.decision_id;
    b.mandate_id   = mandate_id;
    std::memcpy(b.cart_hash, d.cart_hash.data(), 32);
    b.amount_paise = d.amount_paise;
    b.merchant_id  = cart.merchant_id;
    b.nonce        = next_nonce_++;
    b.exp_ns       = now_ns + 60ull * 1000000000ull;   // 60s TTL
    b.wal_seq      = d.wal_seq;
    std::memcpy(b.wal_record_hash, rec_head.data(), 32);
    d.pct     = pct_mint(key_, b);
    d.has_pct = true;
    wal_->append(RecType::CAPABILITY_ISSUED, &b, sizeof b);
  } else {
    wal_->append(RecType::CAPABILITY_DENIED, &d.verdict.bits, sizeof d.verdict.bits);
  }
  if (!group_commit_) wal_->commit();
  return d;
}

double Gateway::measure_last_kernel_ns(int batch, int rounds) const {
  if (!last_n_) return 0.0;
  const CartView c{last_sku_, last_up_, last_qty_, last_n_, last_merchant_};
  std::uint32_t sink = 0;
  for (int i = 0; i < 20000; ++i) sink ^= evaluate(last_schema_, c, last_now_).bits;  // warm

  std::vector<double> per;
  per.reserve(static_cast<std::size_t>(rounds));
  for (int r = 0; r < rounds; ++r) {
    const std::uint64_t t0 = mono_ns();
    for (int i = 0; i < batch; ++i) {
      sink ^= evaluate(last_schema_, c, last_now_).bits;
      asm volatile("" :: "r"(sink) : "memory");
    }
    per.push_back(double(mono_ns() - t0) / batch);
  }
  std::sort(per.begin(), per.end());
  return per[per.size() / 2];
}

static void esc(std::ostringstream& o, std::string_view s) {
  for (char c : s) {
    if (c == '"' || c == '\\') { o << '\\' << c; }
    else if (static_cast<unsigned char>(c) < 0x20) { o << ' '; }
    else o << c;
  }
}

std::string Gateway::decision_json(const Decision& d) const {
  std::ostringstream o;
  o << "{\"decision_id\":" << d.decision_id
    << ",\"mandate_id\":" << d.mandate_id
    << ",\"decision\":\"" << (d.verdict.allowed() ? "ALLOW" : "DENY") << "\""
    << ",\"verdict_bits\":" << d.verdict.bits
    << ",\"verdict_hex\":\"0x" << std::hex;
  o.width(4); o.fill('0'); o << d.verdict.bits << std::dec << "\""
    << ",\"eval_ns\":" << d.eval_ns
    << ",\"cart_total_paise\":" << d.verdict.cart_total_paise
    << ",\"wal_seq\":" << d.wal_seq
    << ",\"commit_us\":" << d.commit_us
    << ",\"cart_hash\":\"" << hex(d.cart_hash) << "\""
    << ",\"capability_issued\":" << (d.has_pct ? "true" : "false");
  if (!d.parse_error.empty()) { o << ",\"error\":\""; esc(o, d.parse_error); o << "\""; }

  o << ",\"reasons\":[";
  bool first = true;
  for (int b = 0; b < R_BIT_COUNT; ++b) {
    const std::uint32_t bit = 1u << b;
    if (!(d.verdict.bits & bit)) continue;
    if (!first) o << ",";
    first = false;
    o << "{\"code\":\"" << reject_name(bit) << "\",\"detail\":\"" << reject_help(bit) << "\"}";
  }
  o << "],\"lines\":[";
  for (std::uint32_t i = 0; i < d.verdict.n_lines; ++i) {
    if (i) o << ",";
    o << "{\"sku\":\"";
    esc(o, intern_.name(d.verdict.lines[i].sku_id));
    o << "\",\"bits\":" << d.verdict.lines[i].bits
      << ",\"ok\":" << (d.verdict.lines[i].bits == R_NONE ? "true" : "false") << "}";
  }
  o << "]}";
  return o.str();
}

std::string Gateway::repair_hint_json(const Decision& d) const {
  // Graceful failure: a denial is not a dead end. Tell the agent exactly what to drop,
  // and tell the user how to approve it deliberately if they actually wanted it.
  std::ostringstream o;
  o << "{\"remove\":[";
  bool first = true;
  for (std::uint32_t i = 0; i < d.verdict.n_lines; ++i) {
    if (d.verdict.lines[i].bits == R_NONE) continue;
    if (!first) o << ",";
    first = false;
    o << "\"";
    esc(o, intern_.name(d.verdict.lines[i].sku_id));
    o << "\"";
  }
  o << "],\"resubmit_ok\":" << (first ? "false" : "true")
    << ",\"escalate\":{\"path\":\"new_mandate\",\"requires\":\"user_mfa\"}}";
  return o.str();
}

}  // namespace rig
