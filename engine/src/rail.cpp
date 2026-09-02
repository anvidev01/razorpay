#include "rig/rail.hpp"
#include <curl/curl.h>
#include <time.h>
#include <cstdlib>
#include <cstdio>

namespace rig {

static std::uint64_t mono_us() { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1000; }

PaymentResult MockRail::create_order(std::int64_t amount_paise, const std::string& receipt,
                                     const std::string&) {
  PaymentResult r;
  const std::uint64_t t0 = mono_us();
  char buf[64];
  std::snprintf(buf, sizeof buf, "order_MOCK%010llu", (unsigned long long)seq_++);
  r.ok          = amount_paise > 0;
  r.order_id    = buf;
  r.http_status = r.ok ? 200 : 400;
  r.error       = r.ok ? "" : "amount must be positive";
  r.latency_us  = mono_us() - t0;
  r.rail        = "mock";
  (void)receipt;
  return r;
}

static std::size_t sink(void* p, std::size_t sz, std::size_t n, void* ud) {
  static_cast<std::string*>(ud)->append(static_cast<char*>(p), sz * n);
  return sz * n;
}

// Extracts a top-level "id" without pulling simdjson into this TU.
static std::string field(const std::string& body, const char* key) {
  const std::string k = std::string("\"") + key + "\"";
  auto p = body.find(k);
  if (p == std::string::npos) return {};
  p = body.find(':', p + k.size());
  if (p == std::string::npos) return {};
  auto a = body.find('"', p);
  if (a == std::string::npos) return {};
  auto b = body.find('"', a + 1);
  if (b == std::string::npos) return {};
  return body.substr(a + 1, b - a - 1);
}

PaymentResult RazorpayTestRail::create_order(std::int64_t amount_paise,
                                            const std::string& receipt,
                                            const std::string& notes_json) {
  PaymentResult r;
  r.rail = "razorpay-test";
  const std::uint64_t t0 = mono_us();

  CURL* c = curl_easy_init();
  if (!c) { r.error = "curl init failed"; return r; }

  char body[1024];
  std::snprintf(body, sizeof body,
    R"({"amount":%lld,"currency":"INR","receipt":"%s","notes":%s})",
    (long long)amount_paise, receipt.c_str(),
    notes_json.empty() ? "{}" : notes_json.c_str());

  std::string resp;
  curl_slist* hdr = curl_slist_append(nullptr, "Content-Type: application/json");
  curl_easy_setopt(c, CURLOPT_URL, "https://api.razorpay.com/v1/orders");
  curl_easy_setopt(c, CURLOPT_POST, 1L);
  curl_easy_setopt(c, CURLOPT_POSTFIELDS, body);
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
  curl_easy_setopt(c, CURLOPT_USERNAME, key_id_.c_str());
  curl_easy_setopt(c, CURLOPT_PASSWORD, key_secret_.c_str());
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 8000L);

  const CURLcode rc = curl_easy_perform(c);
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.http_status);
  curl_slist_free_all(hdr);
  curl_easy_cleanup(c);

  r.latency_us = mono_us() - t0;
  if (rc != CURLE_OK) { r.error = curl_easy_strerror(rc); return r; }
  if (r.http_status == 200) {
    r.order_id = field(resp, "id");
    r.ok       = !r.order_id.empty();
    if (!r.ok) r.error = "200 but no order id in response";
  } else {
    r.error = field(resp, "description");
    if (r.error.empty()) r.error = "http " + std::to_string(r.http_status);
  }
  return r;
}

std::unique_ptr<PaymentRail> make_rail(std::string& chosen) {
  const char* id  = std::getenv("RAZORPAY_KEY_ID");
  const char* sec = std::getenv("RAZORPAY_KEY_SECRET");
  if (id && sec && *id && *sec) {
    chosen = "razorpay-test";
    return std::make_unique<RazorpayTestRail>(id, sec);
  }
  chosen = "mock";   // no keys -> deterministic offline rail, and we say so
  return std::make_unique<MockRail>();
}

}  // namespace rig
