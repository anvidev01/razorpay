// The payment rail.  [Track 01 — "on Razorpay test-mode APIs"]
//
// This is the ONLY component that talks to Razorpay, and it is unreachable without a
// valid Payment Capability Token. The agent has no credentials and no code path here.
#pragma once
#include <cstdint>
#include <string>
#include <memory>

namespace rig {

struct PaymentResult {
  bool          ok          = false;
  std::string   order_id;              // razorpay order_id (order_...)
  long          http_status = 0;
  std::string   error;
  std::uint64_t latency_us  = 0;
  std::string   rail;                  // which implementation served it
};

class PaymentRail {
public:
  virtual ~PaymentRail() = default;
  virtual PaymentResult create_order(std::int64_t amount_paise,
                                     const std::string& receipt,
                                     const std::string& notes_json) = 0;
  // Reverse a captured payment. Razorpay refunds a PAYMENT, not an order -- an order
  // nobody has paid has nothing to give back, which the rail will say plainly.
  virtual PaymentResult refund(const std::string& payment_id,
                               std::int64_t amount_paise,
                               const std::string& notes_json) = 0;
  virtual const char* name() const noexcept = 0;
};

// Deterministic stand-in so the demo is reproducible offline and in CI.
class MockRail final : public PaymentRail {
public:
  PaymentResult create_order(std::int64_t amount_paise, const std::string& receipt,
                             const std::string& notes_json) override;
  PaymentResult refund(const std::string& payment_id, std::int64_t amount_paise,
                       const std::string& notes_json) override;
  const char* name() const noexcept override { return "mock"; }
private:
  std::uint64_t seq_ = 1;
};

// Real Razorpay Orders API, test mode. Keys come from the environment; they are never
// logged, never written to the WAL, and never visible to the agent.
//   RAZORPAY_KEY_ID / RAZORPAY_KEY_SECRET
class RazorpayTestRail final : public PaymentRail {
public:
  RazorpayTestRail(std::string key_id, std::string key_secret);
  ~RazorpayTestRail() override;
  PaymentResult create_order(std::int64_t amount_paise, const std::string& receipt,
                             const std::string& notes_json) override;
  PaymentResult refund(const std::string& payment_id, std::int64_t amount_paise,
                       const std::string& notes_json) override;
  const char* name() const noexcept override { return "razorpay-test"; }
private:
  std::string key_id_, key_secret_;
  // One handle for the life of the process. A fresh handle per order pays a full DNS
  // + TLS handshake every time -- measured 3.4s cold versus ~180ms once the
  // connection is reused, which is the difference between a demo that looks frozen
  // and one that does not.
  void* curl_ = nullptr;
};

// Picks the real rail when test-mode keys are present, else the mock. Returns which.
std::unique_ptr<PaymentRail> make_rail(std::string& chosen);

}  // namespace rig
