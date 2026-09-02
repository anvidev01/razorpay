#include "rig/gateway.hpp"
#include "rig/clock.hpp"
#include <ctime>
#include <time.h>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <vector>
#include <algorithm>

namespace rig {


Gateway::Gateway(std::string wal_path)
    : wal_path_(wal_path), wal_(std::make_unique<Wal>(wal_path)) {
  for (int i = 0; i < 16; ++i) tag_key_[i] = static_cast<std::uint8_t>(0xA5 ^ (i * 31));
  rail_ = make_rail(rail_name_);
  rebuild_idempotency();
}

// An agent retrying after the gateway restarted must still not be charged twice, so
// the retry window is rebuilt from the durable log rather than living only in RAM.
void Gateway::rebuild_idempotency() {
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  const std::uint64_t now = std::uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
  std::uint32_t restored = 0;
  wal_scan(wal_path_, [&](const WalRecord& r) {
    if (static_cast<RecType>(r.hdr.type) != RecType::POLICY_DECISION) return true;
    if (r.payload.size() != sizeof(DecisionPayload)) return true;
    DecisionPayload dp;
    std::memcpy(&dp, r.payload.data(), sizeof dp);
    if (dp.now_ns + IdempotencyStore::TTL_NS < now) return true;   // outside the window
    Hash256 k{};
    std::memcpy(k.data(), dp.idem_key, 32);
    if (k == Hash256{}) return true;
    idem_.insert(k, r.hdr.seq, r.hdr.seq, dp.recorded_bits == R_NONE, dp.now_ns);
    ++restored;
    return true;
  });

  // Behavioural baselines are durable for the same reason the retry window is: an
  // agent that restarts must not get a clean slate, or the velocity control is
  // trivially defeated by crashing between purchases.
  std::uint32_t profiles = 0;
  wal_scan(wal_path_, [&](const WalRecord& r) {
    if (static_cast<RecType>(r.hdr.type) != RecType::POLICY_DECISION) return true;
    if (r.payload.size() != sizeof(DecisionPayload)) return true;
    DecisionPayload dp;
    std::memcpy(&dp, r.payload.data(), sizeof dp);
    if (dp.outcome != static_cast<std::uint8_t>(Outcome::ALLOW)) return true;
    if (!dp.agent_session_id) return true;
    const std::time_t secs = static_cast<std::time_t>(dp.now_ns / 1000000000ull);
    std::tm tmv{}; localtime_r(&secs, &tmv);
    risk_.observe(risk_.profile(dp.agent_session_id), dp.merchant_id,
                  dp.recorded_total, dp.now_ns, tmv.tm_hour);
    ++profiles;
    return true;
  });
  if (profiles)
    std::fprintf(stderr, "gateway: replayed %u completed decisions into agent baselines\n",
                 profiles);
  if (restored)
    std::fprintf(stderr, "gateway: restored %u in-window decisions into the retry table\n",
                 restored);
}

Tag128 Gateway::tag_of(const IntentSchema& s) const noexcept {
  IntentSchema copy = s;
  std::memset(copy.integrity_tag, 0, sizeof copy.integrity_tag);   // tag excludes itself
  return siphash(&copy, sizeof copy, tag_key_);
}

void Gateway::enroll_device(const std::array<std::uint8_t, 32>& pub, std::string label) {
  enrolled_pub_    = pub;
  enrolled_label_  = std::move(label);
  device_enrolled_ = true;
}

bool Gateway::admit_mandate(const std::string& intent_json, const Sig512& sig,
                            const std::array<std::uint8_t, 32>& pub, std::string& err) {
  if (!device_enrolled_) { err = "no user device enrolled"; return false; }

  // 1. The signature must come from the device the user actually enrolled. Accepting
  //    any key that presents itself would make the signature decorative.
  if (pub != enrolled_pub_) {
    err = "mandate signed by an unenrolled device (" + hex(pub.data(), 8)
        + "), expected " + hex(enrolled_pub_.data(), 8);
    return false;
  }
  // 2. The signature must cover these EXACT bytes, so nothing can be edited between
  //    what the human approved on their phone and what the engine enforces.
  //    Ed25519 costs ~36.5us and is paid ONCE here, never per cart.
  if (!verify_detached(pub.data(), intent_json.data(), intent_json.size(), sig)) {
    err = "signature does not cover this mandate (altered after signing?)";
    return false;
  }

  IntentSchema s{};
  const ParseResult pr = parse_intent(intent_json, intern_, s);
  if (!pr.ok) { err = pr.error; return false; }
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

  struct { std::uint64_t id; std::uint8_t sig[64]; std::uint8_t dev[32]; } rec{};
  rec.id = s.mandate_id;
  std::memcpy(rec.sig, sig.data(), 64);
  std::memcpy(rec.dev, pub.data(), 32);
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
  const ParseResult pr = parse_cart(cart_json, intern_, arena_, cart, d.cart_hash,
                                    mandate_id, &d.agent_session_id);
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
  // ---- Track 02: behavioural signals. These ESCALATE to the human, never auto-block.
  {
    const std::time_t secs = static_cast<std::time_t>(now_ns / 1000000000ull);
    std::tm tmv{};
    localtime_r(&secs, &tmv);
    AgentProfile& prof = risk_.profile(d.agent_session_id);
    d.risk_bits = risk_.assess(prof, cart.merchant_id, d.verdict.cart_total_paise,
                               now_ns, tmv.tm_hour);
    d.verdict.bits |= d.risk_bits;
  }
  d.merchant_id = cart.merchant_id;
  d.outcome = classify(d.verdict.bits);

  last_flags_ = cart.text_flags;
  for (std::uint32_t i = 0; i < last_n_; ++i) {
    last_sku_[i] = cart.sku_id[i]; last_up_[i] = cart.unit_paise[i]; last_qty_[i] = cart.qty[i];
    last_cat_[i] = cart.category_id ? cart.category_id[i] : 0u;
  }
  d.amount_paise = d.verdict.cart_total_paise;

  // ---- AGENT RETRY: collapse a re-generated request onto its original decision ----
  // Keyed on the canonical interned cart, so different JSON spelling of the same
  // shopping cart lands on the same key. This is what stops the double charge.
  d.idem_key = IdempotencyStore::key_of(mandate_id, d.cart_hash,
                                        cart.merchant_id, d.amount_paise);
  if (IdemEntry* prior = idem_.find(d.idem_key, now_ns)) {
    ++prior->hits;
    d.duplicate_suppressed = true;
    d.original_decision_id = prior->decision_id;
    d.duplicate_hits       = prior->hits;
    d.wal_seq              = prior->wal_seq;
    d.verdict.bits        |= R_DUPLICATE_CHARGE;
    // Re-classify: the outcome computed above predates the duplicate bit. A duplicate
    // is refused deterministically -- it can never be reviewed into a second charge.
    d.outcome              = classify(d.verdict.bits);
    // Idempotent semantics: replay the ORIGINAL outcome, mint nothing new.
    struct { std::uint64_t orig; std::uint64_t seq; std::uint32_t hits; std::uint8_t key[32]; } rec{};
    rec.orig = prior->decision_id; rec.seq = prior->wal_seq; rec.hits = prior->hits;
    std::memcpy(rec.key, d.idem_key.data(), 32);
    wal_->append(RecType::DUPLICATE_SUPPRESSED, &rec, sizeof rec);
    if (!group_commit_) wal_->commit();
    return d;
  }

  // --- record the full inputs so the auditor can re-execute this decision offline ---
  DecisionPayload dp{};
  dp.schema      = s;
  dp.now_ns      = now_ns;
  dp.merchant_id = cart.merchant_id;
  dp.n_lines     = cart.n;
  for (std::uint32_t i = 0; i < cart.n && i < MAX_CART; ++i) {
    dp.sku_id[i]      = cart.sku_id[i];
    dp.unit_paise[i]  = cart.unit_paise[i];
    dp.qty[i]         = cart.qty[i];
    dp.category_id[i] = cart.category_id ? cart.category_id[i] : 0u;
  }
  dp.text_flags = cart.text_flags;
  dp.agent_session_id = d.agent_session_id;
  dp.risk_bits        = d.risk_bits;
  dp.outcome          = static_cast<std::uint8_t>(d.outcome);
  dp.recorded_bits  = d.verdict.bits;
  dp.recorded_total = d.verdict.cart_total_paise;
  dp.eval_ns        = d.eval_ns;
  std::memcpy(&dp.cart_hash_lo, d.cart_hash.data(), 8);
  std::memcpy(dp.idem_key, d.idem_key.data(), 32);

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

  if (d.outcome == Outcome::REVIEW) {
    // Park it and ask the human. No token exists yet, so no money can move.
    struct { std::uint64_t id, seq, sess; std::uint32_t risk; } sr{};
    sr.id = d.decision_id; sr.seq = d.wal_seq;
    sr.sess = d.agent_session_id; sr.risk = d.risk_bits;
    wal_->append(RecType::STEP_UP_REQUIRED, &sr, sizeof sr);
    // Park it. If every slot is live, evict the OLDEST rather than silently dropping
    // this one -- a dropped step-up is a decision the human can never answer, which
    // strands the agent instead of failing cleanly.
    Pending* slot = nullptr;
    for (auto& p : pending_) if (!p.live) { slot = &p; break; }
    if (!slot) {
      slot = &pending_[0];
      for (auto& p : pending_) if (p.decision_id < slot->decision_id) slot = &p;
      std::fprintf(stderr, "gateway: step-up table full, evicting decision %llu\n",
                   (unsigned long long)slot->decision_id);
    }
    *slot = Pending{d.decision_id, mandate_id, d.wal_seq, d.amount_paise,
                    cart.merchant_id, d.agent_session_id, d.cart_hash, rec_head, true};
    if (!group_commit_) wal_->commit();
    idem_.insert(d.idem_key, d.decision_id, d.wal_seq, false, now_ns);
    return d;
  }

  if (d.outcome == Outcome::ALLOW) {
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
  // Remember this cart so the agent's next retry collapses onto it rather than
  // becoming a second charge.
  idem_.insert(d.idem_key, d.decision_id, d.wal_seq, d.outcome == Outcome::ALLOW, now_ns);
  return d;
}

bool Gateway::confirm(std::uint64_t decision_id, bool approved, const std::string& human_ref,
                      std::uint64_t now_ns, Decision& out) {
  Pending* p = nullptr;
  for (auto& q : pending_) if (q.live && q.decision_id == decision_id) { p = &q; break; }
  if (!p) return false;

  out = Decision{};
  out.decision_id      = p->decision_id;
  out.mandate_id       = p->mandate_id;
  out.wal_seq          = p->wal_seq;
  out.amount_paise     = p->amount_paise;
  out.cart_hash        = p->cart_hash;
  out.agent_session_id = p->agent_session_id;
  out.merchant_id      = p->merchant_id;
  out.parsed           = true;

  // The human's answer, bound to THIS decision and THIS cart hash.
  struct { std::uint64_t id, at; std::uint8_t approved; char ref[32]; std::uint8_t cart[32]; } hc{};
  hc.id = decision_id; hc.at = now_ns; hc.approved = approved ? 1 : 0;
  std::snprintf(hc.ref, sizeof hc.ref, "%s", human_ref.c_str());
  std::memcpy(hc.cart, p->cart_hash.data(), 32);
  wal_->append(RecType::HUMAN_CONFIRMED, &hc, sizeof hc);

  out.confirmation.present  = true;
  out.confirmation.approved = approved;
  out.confirmation.at_ns    = now_ns;
  std::snprintf(out.confirmation.human_ref, sizeof out.confirmation.human_ref, "%s",
                human_ref.c_str());

  if (approved) {
    out.outcome = Outcome::ALLOW;
    PctBody b{};
    b.decision_id  = p->decision_id;
    b.mandate_id   = p->mandate_id;
    std::memcpy(b.cart_hash, p->cart_hash.data(), 32);
    b.amount_paise = p->amount_paise;
    b.merchant_id  = p->merchant_id;
    b.nonce        = next_nonce_++;
    b.exp_ns       = now_ns + 60ull * 1000000000ull;
    b.wal_seq      = p->wal_seq;
    std::memcpy(b.wal_record_hash, p->rec_head.data(), 32);
    out.pct     = pct_mint(key_, b);
    out.has_pct = true;
    wal_->append(RecType::CAPABILITY_ISSUED, &b, sizeof b);
  } else {
    out.outcome = Outcome::DENY;
    const std::uint32_t bits = R_REPLAY_NONCE;   // human declined; nothing is authorised
    wal_->append(RecType::CAPABILITY_DENIED, &bits, sizeof bits);
    struct { std::uint64_t id; char why[32]; } rem{};
    rem.id = decision_id;
    std::snprintf(rem.why, sizeof rem.why, "human declined at step-up");
    wal_->append(RecType::REMEDIATION, &rem, sizeof rem);
  }
  wal_->commit();
  p->live = false;
  return true;
}

bool Gateway::execute(Decision& d, std::uint64_t now_ns) {
  if (!d.has_pct) return false;
  // The executor re-checks the token against the cart it is about to submit.
  const PctStatus st = exec_.authorize(&d.pct, d.cart_hash, d.amount_paise, now_ns);
  if (st != PctStatus::VALID) {
    d.payment.error = pct_status_name(st);
    wal_->append(RecType::PAYMENT_RESULT, &d.decision_id, sizeof d.decision_id);
    wal_->commit();
    return false;
  }
  struct { std::uint64_t id; std::int64_t amount; } att{ d.decision_id, d.amount_paise };
  wal_->append(RecType::PAYMENT_ATTEMPTED, &att, sizeof att);
  wal_->commit();                       // attempt is durable BEFORE money moves

  char receipt[64];
  std::snprintf(receipt, sizeof receipt, "rig_%llu", (unsigned long long)d.decision_id);
  char notes[256];
  std::snprintf(notes, sizeof notes,
    R"({"mandate_id":"%llu","decision_id":"%llu","wal_seq":"%llu","gateway":"intent-gateway"})",
    (unsigned long long)d.mandate_id, (unsigned long long)d.decision_id,
    (unsigned long long)d.wal_seq);
  d.payment = rail_->create_order(d.amount_paise, receipt, notes);
  d.paid    = d.payment.ok;

  // The rail's own reason is part of the audit trail: "every money action explainable"
  // has to cover the ones that failed, or a dispute over a failed charge has no record.
  struct { std::uint64_t id; std::uint8_t ok; long status; char order[40]; char err[80]; } res{};
  res.id = d.decision_id; res.ok = d.payment.ok ? 1 : 0; res.status = d.payment.http_status;
  std::snprintf(res.order, sizeof res.order, "%s", d.payment.order_id.c_str());
  std::snprintf(res.err, sizeof res.err, "%s", d.payment.error.c_str());
  wal_->append(RecType::PAYMENT_RESULT, &res, sizeof res);
  wal_->commit();

  if (d.paid) {   // only completed payments shape the behavioural baseline
    const std::time_t secs = static_cast<std::time_t>(now_ns / 1000000000ull);
    std::tm tmv{}; localtime_r(&secs, &tmv);
    risk_.observe(risk_.profile(d.agent_session_id), d.merchant_id, d.amount_paise,
                  now_ns, tmv.tm_hour);
  }
  return d.paid;
}

double Gateway::measure_last_kernel_ns(int batch, int rounds) const {
  if (!last_n_) return 0.0;
  const CartView c{last_sku_, last_up_, last_qty_, last_cat_, last_n_, last_merchant_, last_flags_, 0};
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
    << ",\"decision\":\"" << outcome_name(d.outcome) << "\""
    << ",\"risk_bits\":" << d.risk_bits
    << ",\"agent_session_id\":" << d.agent_session_id
    << ",\"paid\":" << (d.paid ? "true" : "false")
    << ",\"payment_order_id\":\"" << d.payment.order_id << "\""
    << ",\"rail\":\"" << (rail_ ? rail_->name() : "none") << "\""
    << ",\"verdict_bits\":" << d.verdict.bits
    << ",\"verdict_hex\":\"0x" << std::hex;
  o.width(4); o.fill('0'); o << d.verdict.bits << std::dec << "\""
    << ",\"eval_ns\":" << d.eval_ns
    << ",\"cart_total_paise\":" << d.verdict.cart_total_paise
    << ",\"wal_seq\":" << d.wal_seq
    << ",\"commit_us\":" << d.commit_us
    << ",\"cart_hash\":\"" << hex(d.cart_hash) << "\""
    << ",\"capability_issued\":" << (d.has_pct ? "true" : "false")
    << ",\"duplicate_suppressed\":" << (d.duplicate_suppressed ? "true" : "false")
    << ",\"original_decision_id\":" << d.original_decision_id
    << ",\"duplicate_hits\":" << d.duplicate_hits
    << ",\"idempotency_key\":\"" << hex(d.idem_key).substr(0, 16) << "\"";
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
      << ",\"ok\":" << (d.verdict.lines[i].bits == R_NONE ? "true" : "false");
    if (d.verdict.lines[i].substituted_for) {
      o << ",\"substituted_for\":\"";
      esc(o, intern_.name(d.verdict.lines[i].substituted_for));
      o << "\"";
    }
    o << "}";
  }
  o << "]}";
  return o.str();
}

std::string Gateway::repair_hint_json(const Decision& d) const {
  // Graceful failure: a denial is not a dead end. Tell the agent exactly what to drop,
  // and tell the user how to approve it deliberately if they actually wanted it.
  std::ostringstream o;
  // A duplicate is not a rejection to repair -- it is the same answer again. Telling
  // the agent to "remove items and resubmit" here would push it into a retry loop,
  // which is the exact failure this feature exists to stop.
  if (d.outcome == Outcome::REVIEW) {
    o << "{\"action\":\"await_human_confirmation\",\"decision_id\":" << d.decision_id
      << ",\"reason\":\"behavioural signal only; the cart itself is within intent\""
      << ",\"requires\":\"user_mfa\",\"resubmit_ok\":false}";
    return o.str();
  }
  if (d.duplicate_suppressed) {
    o << "{\"duplicate_of_decision\":" << d.original_decision_id
      << ",\"wal_seq\":" << d.wal_seq
      << ",\"action\":\"stop_retrying\""
      << ",\"detail\":\"this basket was already decided; poll the original decision"
         " rather than resubmitting\",\"resubmit_ok\":false}";
    return o.str();
  }
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
