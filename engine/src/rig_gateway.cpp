// rig-gateway: minimal HTTP/1.1 server exposing the engine to the demo UI.
// Single-threaded and deliberately small -- the interesting code is the engine.
#include "rig/gateway.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
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
    << "Access-Control-Allow-Origin: *\r\n\r\n" << body;
  send_all(fd, o.str());
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

  auto gw = std::make_unique<Gateway>(wal_path);
  { std::string err;
    const std::string intent = slurp("fixtures/lunch_intent.json");
    if (!intent.empty() && !gw->admit_mandate(intent, err))
      std::fprintf(stderr, "warning: default mandate not admitted: %s\n", err.c_str()); }

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
    std::string req; req.reserve(8192);
    char buf[4096];
    ssize_t n;
    while ((n = ::read(c, buf, sizeof buf)) > 0) {
      req.append(buf, (std::size_t)n);
      const auto hp = req.find("\r\n\r\n");
      if (hp == std::string::npos) continue;
      std::size_t clen = 0;
      const auto cl = req.find("Content-Length:");
      if (cl != std::string::npos) clen = std::strtoul(req.c_str() + cl + 15, nullptr, 10);
      if (req.size() >= hp + 4 + clen) break;
    }
    if (req.empty()) { ::close(c); continue; }

    const auto sp1 = req.find(' ');
    const auto sp2 = req.find(' ', sp1 + 1);
    const std::string method = req.substr(0, sp1);
    const std::string path   = req.substr(sp1 + 1, sp2 - sp1 - 1);
    const auto hp   = req.find("\r\n\r\n");
    const std::string body = hp == std::string::npos ? "" : req.substr(hp + 4);

    if (method == "POST" && path == "/api/decide") {
      const Decision d = gw->decide(body, now_ns());
      std::ostringstream o;
      o << "{\"decision\":" << gw->decision_json(d)
        << ",\"repair\":" << gw->repair_hint_json(d)
        << ",\"kernel_ns_batched\":" << gw->measure_last_kernel_ns(500, 101) << "}";
      respond(c, 200, "application/json", o.str());
    } else if (method == "POST" && path == "/api/admit") {
      std::string err;
      const bool ok = gw->admit_mandate(body, err);
      respond(c, ok ? 200 : 400, "application/json",
              std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"error\":\"" + err + "\"}");
    } else if (method == "GET" && path.rfind("/api/wal", 0) == 0) {
      respond(c, 200, "application/json", wal_json(wal_path, 40));
    } else if (method == "GET" && path == "/api/health") {
      respond(c, 200, "application/json", "{\"ok\":true}");
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
