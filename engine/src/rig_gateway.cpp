// rig-gateway: minimal HTTP/1.1 server exposing the engine to the demo UI.
// Single-threaded and deliberately small -- the interesting code is the engine.
#include "rig/gateway.hpp"
#include "rig/evidence.hpp"
#include "rig/intent.hpp"
#include <map>
#include <cctype>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <memory>

using namespace rig;

static std::string slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  std::ostringstream o; o << f.rdbuf(); return o.str();
}
static std::uint64_t now_ns() {
  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  return std::uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}
static void send_all(int fd, const std::string& s) {
  const char* p = s.data(); std::size_t left = s.size();
  while (left) { const ssize_t w = ::write(fd, p, left); if (w <= 0) return; p += w; left -= (std::size_t)w; }
}
static void respond(int fd, int code, const char* ctype, const std::string& body) {
  std::ostringstream o;
  o << "HTTP/1.1 " << code << (code == 200 ? " OK" : (code == 404 ? " Not Found" : " Bad Request")) << "\r\n"
    << "Content-Type: " << ctype << "\r\n"
    << "Content-Length: " << body.size() << "\r\n"
    << "Cache-Control: no-store\r\n"
    // No Access-Control-Allow-Origin. The UI is served by this same origin, so CORS
    // buys nothing -- and a wildcard on an endpoint that moves money lets any page the
    // user happens to be visiting POST to 127.0.0.1 and spend from a live mandate.
    << "X-Content-Type-Options: nosniff\r\n"
    << "Cache-Control: no-store\r\n\r\n" << body;
  send_all(fd, o.str());
}

// Tiny field readers so the server has no JSON dependency of its own.
static std::uint64_t json_u64(const std::string& b, const char* key, std::uint64_t dflt = 0) {
  const std::string k = std::string("\"") + key + "\"";
  auto p = b.find(k); if (p == std::string::npos) return dflt;
  p = b.find(':', p + k.size()); if (p == std::string::npos) return dflt;
  return std::strtoull(b.c_str() + p + 1, nullptr, 10);
}
static bool json_bool(const std::string& b, const char* key, bool dflt = false) {
  const std::string k = std::string("\"") + key + "\"";
  auto p = b.find(k); if (p == std::string::npos) return dflt;
  p = b.find(':', p + k.size()); if (p == std::string::npos) return dflt;
  return b.compare(b.find_first_not_of(" \t", p + 1), 4, "true") == 0;
}
static std::string json_str(const std::string& b, const char* key, const char* dflt = "") {
  const std::string k = std::string("\"") + key + "\"";
  auto p = b.find(k); if (p == std::string::npos) return dflt;
  p = b.find(':', p + k.size()); if (p == std::string::npos) return dflt;
  auto a = b.find('"', p); if (a == std::string::npos) return dflt;
  auto e = b.find('"', a + 1); if (e == std::string::npos) return dflt;
  return b.substr(a + 1, e - a - 1);
}
// Case-insensitive header lookup; the value is the rest of the line, trimmed.
static std::string header_value(const std::string& req, const char* name) {
  std::string lower = req, key = name;
  for (auto& ch : lower) ch = static_cast<char>(::tolower((unsigned char)ch));
  for (auto& ch : key)   ch = static_cast<char>(::tolower((unsigned char)ch));
  auto p = lower.find("\r\n" + key + ":");
  if (p == std::string::npos) return {};
  p = req.find(':', p + 2) + 1;
  const auto e = req.find("\r\n", p);
  std::string v = req.substr(p, e - p);
  while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
  while (!v.empty() && (v.back() == ' ' || v.back() == '\r')) v.pop_back();
  return v;
}

static bool unhex(const std::string& h, std::uint8_t* out, std::size_t n) {
  if (h.size() != n * 2) return false;
  for (std::size_t i = 0; i < n; ++i) {
    auto nib = [](char ch) -> int {
      if (ch >= '0' && ch <= '9') return ch - '0';
      if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
      if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
      return -1;
    };
    const int hi = nib(h[2 * i]), lo = nib(h[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  return true;
}

static std::uint64_t query_u64(const std::string& path, const char* key) {
  const std::string k = std::string(key) + "=";
  auto p = path.find(k);
  return p == std::string::npos ? 0 : std::strtoull(path.c_str() + p + k.size(), nullptr, 10);
}

// Renders the readable audit trail the UI shows -- same content as rig-audit.
static std::string audit_json(const std::string& path) {
  std::vector<std::string> rows;
  std::uint64_t allow = 0, review = 0, deny = 0, paid = 0, dupes = 0;
  const ChainReport rep = wal_scan(path, [&](const WalRecord& r) {
    const auto t = static_cast<RecType>(r.hdr.type);
    char buf[900];
    std::string detail, kind = "info";
    if (t == RecType::POLICY_DECISION && r.payload.size() == sizeof(DecisionPayload)) {
      DecisionPayload dp; std::memcpy(&dp, r.payload.data(), sizeof dp);
      const auto oc = static_cast<Outcome>(dp.outcome);
      if (oc == Outcome::ALLOW) { ++allow; kind = "good"; }
      else if (oc == Outcome::REVIEW) { ++review; kind = "warn"; }
      else { ++deny; kind = "bad"; }
      std::string reasons;
      for (int b = 0; b < static_cast<int>(R_BIT_COUNT); ++b) {
        const std::uint32_t bit = 1u << b;
        if (!(dp.recorded_bits & bit)) continue;
        if (!reasons.empty()) reasons += ", ";
        reasons += reject_name(bit);
      }
      std::snprintf(buf, sizeof buf, "%s 0x%04X  Rs %.2f  %u lines%s%s",
        outcome_name(oc), dp.recorded_bits, dp.recorded_total / 100.0, dp.n_lines,
        reasons.empty() ? "" : "  |  ", reasons.c_str());
      detail = buf;
    } else if (t == RecType::MANDATE_ISSUED) {
      kind = "good"; detail = "human signed an intent mandate (Ed25519)";
    } else if (t == RecType::CART_PROPOSED) {
      detail = "agent proposed a cart (" + std::to_string(r.payload.size()) + " bytes)";
    } else if (t == RecType::CAPABILITY_ISSUED) {
      kind = "good"; detail = "payment token minted (single use, cart-bound)";
    } else if (t == RecType::CAPABILITY_DENIED) {
      kind = "bad";  detail = "no token -- cannot reach the rail";
    } else if (t == RecType::STEP_UP_REQUIRED) {
      kind = "warn"; detail = "escalated to the human (not blocked)";
    } else if (t == RecType::HUMAN_CONFIRMED) {
      struct HC { std::uint64_t id, at; std::uint8_t ok; char ref[32]; std::uint8_t cart[32]; };
      if (r.payload.size() >= sizeof(HC)) {
        HC hc; std::memcpy(&hc, r.payload.data(), sizeof hc);
        kind = hc.ok ? "good" : "bad";
        std::snprintf(buf, sizeof buf, "human %s (ref %s) bound to decision #%llu",
          hc.ok ? "APPROVED" : "DECLINED", hc.ref, (unsigned long long)hc.id);
        detail = buf;
      }
    } else if (t == RecType::PAYMENT_ATTEMPTED) {
      detail = "submitting to the payment rail";
    } else if (t == RecType::PAYMENT_RESULT) {
      struct PR { std::uint64_t id; std::uint8_t ok; long status; char order[40]; char err[80]; };
      if (r.payload.size() >= sizeof(PR)) {
        PR pr; std::memcpy(&pr, r.payload.data(), sizeof pr);
        kind = pr.ok ? "good" : "bad";
        if (pr.ok) ++paid;
        // A failed payment must carry the rail's own reason into the UI, not just a
        // status code -- a dispute over a failed charge needs to say why it failed.
        std::snprintf(buf, sizeof buf, "%s  http %ld  %s",
          pr.ok ? "PAID" : "FAILED", pr.status,
          pr.ok ? pr.order : (pr.err[0] ? pr.err : "no reason recorded"));
        detail = buf;
      }
    } else if (t == RecType::DUPLICATE_SUPPRESSED) {
      kind = "warn"; ++dupes;
      detail = "agent retry collapsed onto the original decision -- no second charge";
    } else if (t == RecType::REMEDIATION) {
      detail = "remediation recorded";
    }
    char row[1100];
    std::snprintf(row, sizeof row,
      R"({"seq":%llu,"type":"%s","kind":"%s","hash":"%s","detail":"%s"})",
      (unsigned long long)r.hdr.seq, rectype_name(t), kind.c_str(),
      hex(r.this_hash).substr(0, 12).c_str(), detail.c_str());
    rows.emplace_back(row);
    return true;
  });
  std::ostringstream o;
  o << R"({"intact":)" << (rep.intact ? "true" : "false")
    << R"(,"records":)" << rep.records
    << R"(,"allow":)" << allow << R"(,"review":)" << review << R"(,"deny":)" << deny
    << R"(,"paid":)" << paid << R"(,"dupes":)" << dupes << R"(,"rows":[)";
  for (std::size_t i = 0; i < rows.size(); ++i) { if (i) o << ","; o << rows[i]; }
  o << "]}";
  return o.str();
}

static std::string wal_json(const std::string& path, std::size_t tail) {
  std::vector<std::string> rows;
  const ChainReport rep = wal_scan(path, [&](const WalRecord& r) {
    char buf[512];
    const auto t = static_cast<RecType>(r.hdr.type);
    int k = std::snprintf(buf, sizeof buf,
      R"({"seq":%llu,"type":"%s","hash":"%s")",
      (unsigned long long)r.hdr.seq, rectype_name(t), hex(r.this_hash).substr(0, 12).c_str());
    if (t == RecType::POLICY_DECISION && r.payload.size() == sizeof(DecisionPayload)) {
      DecisionPayload dp; std::memcpy(&dp, r.payload.data(), sizeof dp);
      k += std::snprintf(buf + k, sizeof buf - k,
        R"(,"verdict":"0x%04X","total_paise":%lld,"lines":%u)",
        dp.recorded_bits, (long long)dp.recorded_total, dp.n_lines);
    }
    std::snprintf(buf + k, sizeof buf - k, "}");
    rows.emplace_back(buf);
    return true;
  });
  std::ostringstream o;
  o << R"({"intact":)" << (rep.intact ? "true" : "false")
    << R"(,"records":)" << rep.records << R"(,"rows":[)";
  const std::size_t start = (tail && rows.size() > tail) ? rows.size() - tail : 0;
  for (std::size_t i = start; i < rows.size(); ++i) { if (i > start) o << ","; o << rows[i]; }
  o << "]}";
  return o.str();
}

static int run(int argc, char** argv) {
  ::signal(SIGPIPE, SIG_IGN);
  int port = 8787;
  std::string wal_path = "wal/rig.wal", ui_dir = "ui";
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--wal" && i + 1 < argc) wal_path = argv[++i];
    else if (a == "--ui" && i + 1 < argc) ui_dir = argv[++i];
  }

  std::map<std::uint64_t, Decision> live;   // decisions awaiting confirm / execute

  // One user device for the whole session: it signs, the gateway verifies.
  UserDevice device("user_phone_9f21", "wal/device.key");

  auto admit_default = [&](Gateway& g) {
    g.enroll_device(device.public_key(), device.label());
    std::string err;
    const std::string intent = slurp("fixtures/lunch_intent.json");
    if (!intent.empty() && !g.admit_mandate(intent, device.sign(intent), device.public_key(), err))
      std::fprintf(stderr, "warning: lunch mandate not admitted: %s\n", err.c_str());
    const std::string groc = slurp("fixtures/grocery_intent.json");
    if (!groc.empty() && !g.admit_mandate(groc, device.sign(groc), device.public_key(), err))
      std::fprintf(stderr, "warning: grocery mandate not admitted: %s\n", err.c_str());
  };

  auto gw = std::make_unique<Gateway>(wal_path);
  admit_default(*gw);
  { std::string err; (void)err;
  }

  const int srv = ::socket(AF_INET, SOCK_STREAM, 0);
  int one = 1; ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{}; addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // loopback only
  addr.sin_port = htons((uint16_t)port);
  if (::bind(srv, (sockaddr*)&addr, sizeof addr) < 0) { std::perror("bind"); return 1; }
  ::listen(srv, 64);
  std::printf("rig-gateway on http://127.0.0.1:%d  (wal=%s)\n", port, wal_path.c_str());

  for (;;) {
    const int c = ::accept(srv, nullptr, nullptr);
    if (c < 0) continue;
    // Bounded reads. Without these a single client can grow `req` without limit, or
    // declare a huge Content-Length and hold the (single-threaded) accept loop open
    // forever -- one slow connection would take the whole gateway down.
    static constexpr std::size_t MAX_HEADERS = 16 * 1024;
    static constexpr std::size_t MAX_BODY    = 256 * 1024;
    timeval tv{}; tv.tv_sec = 5;
    ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    std::string req; req.reserve(8192);
    char buf[4096];
    ssize_t n;
    bool too_big = false;
    while ((n = ::read(c, buf, sizeof buf)) > 0) {
      req.append(buf, (std::size_t)n);
      const auto hp = req.find("\r\n\r\n");
      if (hp == std::string::npos) {
        if (req.size() > MAX_HEADERS) { too_big = true; break; }
        continue;
      }
      std::size_t clen = 0;
      const auto cl = req.find("Content-Length:");
      if (cl != std::string::npos) clen = std::strtoul(req.c_str() + cl + 15, nullptr, 10);
      if (clen > MAX_BODY || req.size() > MAX_HEADERS + MAX_BODY) { too_big = true; break; }
      if (req.size() >= hp + 4 + clen) break;
    }
    if (too_big) { respond(c, 413, "text/plain", "request too large"); ::close(c); continue; }
    if (req.empty()) { ::close(c); continue; }

    const auto sp1 = req.find(' ');
    const auto sp2 = req.find(' ', sp1 + 1);
    const std::string method = req.substr(0, sp1);
    const std::string path   = req.substr(sp1 + 1, sp2 - sp1 - 1);
    const auto hp   = req.find("\r\n\r\n");
    const std::string body = hp == std::string::npos ? "" : req.substr(hp + 4);

    if (method == "POST" && path.rfind("/api/decide", 0) == 0) {
      const bool want_exec = path.find("execute=1") != std::string::npos;
      Decision d = gw->decide(body, now_ns());
      if (want_exec && d.has_pct) gw->execute(d, now_ns());
      const double kns = gw->measure_last_kernel_ns(500, 101);
      live[d.decision_id] = d;
      std::ostringstream o;
      o << "{\"decision\":" << gw->decision_json(d)
        << ",\"repair\":" << gw->repair_hint_json(d)
        << ",\"kernel_ns_batched\":" << kns << "}";
      respond(c, 200, "application/json", o.str());

    } else if (method == "POST" && path == "/api/confirm") {
      // The human's out-of-band answer to a REVIEW, then execute if approved.
      const std::uint64_t id = json_u64(body, "decision_id");
      const bool approved    = json_bool(body, "approved");
      const std::string ref  = json_str(body, "ref", "mfa_device_9f21");
      Decision out;
      if (!gw->confirm(id, approved, ref, now_ns(), out)) {
        respond(c, 400, "application/json",
                R"({"ok":false,"error":"no decision awaiting confirmation with that id"})");
      } else {
        if (out.has_pct) gw->execute(out, now_ns());
        live[out.decision_id] = out;
        std::ostringstream o;
        o << "{\"ok\":true,\"approved\":" << (approved ? "true" : "false")
          << ",\"decision\":" << gw->decision_json(out) << "}";
        respond(c, 200, "application/json", o.str());
      }

    } else if (method == "GET" && path.rfind("/api/audit", 0) == 0) {
      respond(c, 200, "application/json", audit_json(wal_path));

    } else if (method == "GET" && path.rfind("/api/evidence", 0) == 0) {
      const std::uint64_t seq = query_u64(path, "seq");
      const auto r = evidence_json(wal_path, seq);
      respond(c, r.found ? 200 : 404, "application/json", r.json);

    } else if (method == "GET" && path == "/.well-known/agent-commerce") {
      // Agent-readable discovery. An AI buyer arriving cold needs to learn, without a
      // human, what this merchant sells and what rules a mandate must satisfy. Serving
      // it at a well-known path is what makes the merchant transactable end to end
      // rather than only after someone hand-writes a mandate.
      std::ostringstream o;
      o << "{\n  \"protocol\": \"razorpay-intent-gateway/1\",\n"
        << "  \"description\": \"Merchant transactable by an AI buyer under a "
           "human-signed intent mandate.\",\n"
        << "  \"mandate\": {\n"
        << "    \"signature\": \"ed25519 over the exact mandate bytes, by the user's "
           "enrolled device\",\n"
        << "    \"amounts\": \"integer paise\",\n"
        << "    \"max_constraints\": " << MAXC << ",\n"
        << "    \"max_cart_lines\": " << MAX_CART << ",\n"
        << "    \"substitution_policies\": [\"deny\",\"same_category\",\"any_in_budget\"]\n"
        << "  },\n"
        << "  \"endpoints\": {\n"
        << "    \"admit\":    { \"method\": \"POST\", \"path\": \"/api/admit\" },\n"
        << "    \"decide\":   { \"method\": \"POST\", \"path\": \"/api/decide?execute=1\" },\n"
        << "    \"confirm\":  { \"method\": \"POST\", \"path\": \"/api/confirm\" },\n"
        << "    \"catalog\":  { \"method\": \"GET\",  \"path\": \"/api/catalog\" },\n"
        << "    \"audit\":    { \"method\": \"GET\",  \"path\": \"/api/audit\" },\n"
        << "    \"evidence\": { \"method\": \"GET\",  \"path\": \"/api/evidence?seq=N\" }\n"
        << "  },\n"
        << "  \"outcomes\": [\"ALLOW\",\"REVIEW\",\"DENY\"],\n"
        << "  \"reject_codes\": [";
      for (int b = 0; b < static_cast<int>(R_BIT_COUNT); ++b) {
        if (b) o << ",";
        o << "\"" << reject_name(1u << b) << "\"";
      }
      o << "],\n  \"rail\": \"" << gw->rail_name() << "\"\n}\n";
      respond(c, 200, "application/json", o.str());

    } else if (method == "POST" && path == "/api/forge") {
      // Attempts to forge the human's authorisation, run live so the UI can show what
      // the CLI bypass suite shows. Each attempt goes through the SAME admit path a
      // real mandate does -- nothing here is simulated.
      const std::string intent = slurp("fixtures/lunch_intent.json");
      UserDevice attacker("attacker_device");
      std::ostringstream o;
      o << "{\"attempts\":[";

      auto attempt = [&](const char* name, const char* what,
                         const std::string& body_json, const Sig512& sig,
                         const std::array<std::uint8_t,32>& pub, bool first) {
        std::string e;
        const bool admitted = gw->admit_mandate(body_json, sig, pub, e);
        if (!first) o << ",";
        o << "{\"attack\":\"" << name << "\",\"what\":\"" << what
          << "\",\"admitted\":" << (admitted ? "true" : "false")
          << ",\"reason\":\"" << (admitted ? "accepted" : e) << "\"}";
      };

      attempt("sign with an attacker's device",
              "a different Ed25519 key signs the same mandate",
              intent, attacker.sign(intent), attacker.public_key(), true);

      std::string tampered = intent;
      const auto at = tampered.find("\"total_budget_paise\"");
      if (at != std::string::npos) {
        const auto colon = tampered.find(':', at);
        const auto comma = tampered.find(',', colon);
        tampered = tampered.substr(0, colon + 1) + " 99999999" + tampered.substr(comma);
      }
      attempt("raise the budget after signing",
              "budget edited from Rs 500 to Rs 999,999 after the human signed",
              tampered, device.sign(intent), device.public_key(), false);

      Sig512 junk{};
      for (std::size_t i = 0; i < junk.size(); ++i) junk[i] = static_cast<std::uint8_t>(i * 7);
      attempt("fabricate a signature",
              "64 bytes of invented signature",
              intent, junk, device.public_key(), false);

      attempt("the enrolled device (control)",
              "the genuine key the human enrolled",
              intent, device.sign(intent), device.public_key(), false);

      o << "],\"enrolled_device\":\"" << gw->device_fingerprint() << "\"}";
      respond(c, 200, "application/json", o.str());

    } else if (method == "GET" && path == "/api/catalog") {
      respond(c, 200, "application/json", slurp("fixtures/catalog.json"));

    } else if (method == "POST" && path == "/api/intent") {
      // Natural language -> a DRAFT mandate. Deliberately NOT signed here: the draft
      // goes back to the human, who confirms it, and only then does /api/admit sign it.
      // The model has no key and no path to admission.
      const std::string utterance = json_str(body, "utterance");
      const std::string catalog   = slurp("fixtures/catalog.json");
      const IntentDraft d = translate_intent(utterance, catalog);
      std::ostringstream o;
      o << "{\"ok\":" << (d.ok ? "true" : "false")
        << ",\"source\":\"" << d.source << "\""
        << ",\"model\":\"" << d.model << "\""
        << ",\"latency_ms\":" << d.latency_ms
        << ",\"input_tokens\":" << d.input_tokens
        << ",\"output_tokens\":" << d.output_tokens
        << ",\"interpretation\":\"" << d.interpretation << "\""
        << ",\"note\":\"" << d.error << "\""
        << ",\"unmatched\":\"" << d.unmatched << "\""
        << ",\"draft\":" << (d.mandate_json.empty() ? "null" : d.mandate_json) << "}";
      respond(c, 200, "application/json", o.str());

    } else if (method == "POST" && path == "/api/reset") {
      // Drop the gateway (releases the WAL lock), truncate, rebuild. Demo repeatability.
      live.clear();
      gw.reset();
      ::truncate(wal_path.c_str(), 0);
      gw = std::make_unique<Gateway>(wal_path);
      admit_default(*gw);
      respond(c, 200, "application/json", R"({"ok":true})");

    } else if (method == "POST" && path == "/api/admit") {
      std::string err;
      // A real user device is remote: it signs on the phone and sends the signature.
      // If the caller supplies one, verify THAT rather than signing on their behalf --
      // signing for them would make the whole check theatre.
      std::array<std::uint8_t, 32> pub{};
      Sig512 sig{};
      const std::string hp_pub = header_value(req, "X-Device-Pubkey");
      const std::string hp_sig = header_value(req, "X-Device-Signature");
      bool ok;
      if (!hp_pub.empty() && !hp_sig.empty()) {
        if (!unhex(hp_pub, pub.data(), 32) || !unhex(hp_sig, sig.data(), 64)) {
          respond(c, 400, "application/json",
                  R"({"ok":false,"error":"malformed X-Device-Pubkey / X-Device-Signature"})");
          ::close(c); continue;
        }
        ok = gw->admit_mandate(body, sig, pub, err);
      } else {
        ok = gw->admit_mandate(body, device.sign(body), device.public_key(), err);
      }
      respond(c, ok ? 200 : 400, "application/json",
              std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"error\":\"" + err + "\"}");
    } else if (method == "GET" && path.rfind("/api/wal", 0) == 0) {
      respond(c, 200, "application/json", wal_json(wal_path, 40));
    } else if (method == "GET" && path == "/api/health") {
      std::ostringstream o;
      o << "{\"ok\":true,\"rail\":\"" << gw->rail_name() << "\"}";
      respond(c, 200, "application/json", o.str());
    } else if (method == "GET") {
      std::string f = (path == "/" ? "/index.html" : path);
      if (f.find("..") != std::string::npos) { respond(c, 400, "text/plain", "no"); ::close(c); continue; }
      const std::string data = slurp(ui_dir + f);
      const char* ct = f.size() > 3 && f.substr(f.size()-3) == ".js"  ? "application/javascript"
                     : f.size() > 4 && f.substr(f.size()-4) == ".css" ? "text/css" : "text/html";
      if (data.empty()) respond(c, 404, "text/plain", "not found");
      else respond(c, 200, ct, data);
    } else {
      respond(c, 404, "text/plain", "not found");
    }
    ::close(c);
  }
}

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "\n  error: %s\n\n", e.what());
    return 4;
  }
}
