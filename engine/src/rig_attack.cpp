// rig-attack: tries to move money without going through the policy engine.
//
// Every one of these is refused by CONSTRUCTION, not by policy. The agent holds no
// Razorpay credentials; the Executor accepts nothing but a valid, unburned,
// cart-bound, unexpired Ed25519 capability token.
#include "rig/gateway.hpp"
#include <cstdio>
#include <stdexcept>
#include <cstring>
#include <fstream>
#include <sstream>
#include <time.h>
#include <unistd.h>

using namespace rig;

static std::string slurp(const char* p) {
  std::ifstream f(p); std::ostringstream o; o << f.rdbuf(); return o.str();
}
static const char* RED = "\033[31m"; static const char* GRN = "\033[32m";
static const char* DIM = "\033[2m";  static const char* BLD = "\033[1m";
static const char* RST = "\033[0m";

static int fails = 0;
static void report_admit(const char* attack, bool admitted, const std::string& why) {
  if (admitted) ++fails;
  std::printf("  %s%-34s%s %s%-8s%s %s%s%s\n",
    BLD, attack, RST,
    admitted ? GRN : RED, admitted ? "ADMITTED" : "REFUSED", RST,
    DIM, admitted ? "mandate accepted" : why.c_str(), RST);
}

static void report(const char* attack, PctStatus got, bool expect_refused) {
  const bool refused = (got != PctStatus::VALID);
  const bool pass    = refused == expect_refused;
  if (!pass) ++fails;
  std::printf("  %s%-34s%s %s%-8s%s %s%s%s\n",
    BLD, attack, RST,
    refused ? RED : GRN, refused ? "REFUSED" : "ALLOWED", RST,
    DIM, pct_status_name(got), RST);
}

static int run(int argc, char** argv) {
  const char* intent = argc > 1 ? argv[1] : "fixtures/lunch_intent.json";
  const char* cart   = argc > 2 ? argv[2] : "fixtures/lunch_cart.json";

  // Own log, cleared each run: the attack suite needs a FRESH authorised decision to
  // attack, and the idempotency layer would (correctly) collapse a repeated baseline
  // cart onto its original decision, leaving nothing to mint a token from.
  const char* awal = "wal/attack.wal";
  ::unlink(awal);
  Gateway gw(awal);
  std::string err;
  UserDevice device("user_phone_9f21");
  gw.enroll_device(device.public_key(), device.label());
  const std::string mandate_json = slurp(intent);
  if (!gw.admit_mandate(mandate_json, device.sign(mandate_json), device.public_key(), err)) {
    std::fprintf(stderr, "admission failed: %s\n", err.c_str());
    return 2;
  }
  // ---- attacks on ADMISSION: forging the human's authorisation itself ----
  std::printf("\n  %sthree ways to forge the human's authorisation%s\n\n", DIM, RST);
  {
    UserDevice attacker("attacker_device");
    std::string e;
    const bool ok = gw.admit_mandate(mandate_json, attacker.sign(mandate_json),
                                     attacker.public_key(), e);
    report_admit("--sign-with-attacker-device", ok, e);
  }
  {
    // Raise the budget AFTER the human signed. The signature covers the exact bytes,
    // so any edit invalidates it.
    std::string tampered = mandate_json;
    const auto at = tampered.find("\"total_budget_paise\"");
    if (at != std::string::npos) {
      const auto colon = tampered.find(':', at);
      const auto comma = tampered.find(',', colon);
      tampered = tampered.substr(0, colon + 1) + " 99999999" + tampered.substr(comma);
    }
    std::string e;
    const bool ok = gw.admit_mandate(tampered, device.sign(mandate_json),
                                     device.public_key(), e);
    report_admit("--raise-budget-after-signing", ok, e);
  }
  {
    std::string e;
    Sig512 junk{};
    for (std::size_t i = 0; i < junk.size(); ++i) junk[i] = static_cast<std::uint8_t>(i);
    const bool ok = gw.admit_mandate(mandate_json, junk, device.public_key(), e);
    report_admit("--fabricate-a-signature", ok, e);
  }
  {   // control: the genuine device is still accepted
    std::string e;
    const bool ok = gw.admit_mandate(mandate_json, device.sign(mandate_json),
                                     device.public_key(), e);
    std::printf("  %s%-34s%s %s%-8s%s %sthe enrolled device%s\n",
      BLD, "enrolled device (control)", RST, ok ? GRN : RED,
      ok ? "ADMITTED" : "REFUSED", RST, DIM, RST);
    if (!ok) ++fails;
  }

  struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
  const std::uint64_t now = std::uint64_t(ts.tv_sec) * 1000000000ull + ts.tv_nsec;

  const Decision d = gw.decide(slurp(cart), now);
  if (!d.has_pct) { std::fprintf(stderr, "baseline cart was denied; attacks need a valid PCT\n"); return 2; }
  Executor& ex = gw.executor();

  std::printf("\n  %sfive ways to route around the policy engine%s\n\n", DIM, RST);

  // 1. no token at all -- the agent simply calls the payment API itself
  report("--force-payment --skip-gateway",
         ex.authorize(nullptr, d.cart_hash, d.amount_paise, now), true);

  // 2. forge a token -- requires the gateway's Ed25519 private key
  { Pct forged = d.pct; forged.sig[0] ^= 0x01;
    report("--forge-pct", ex.authorize(&forged, d.cart_hash, d.amount_paise, now), true); }

  // 3. swap the cart AFTER approval -- the token commits to the cart hash
  { Hash256 blender_hash = sha256("a completely different cart", 27);
    report("--swap-cart-after-approval",
           ex.authorize(&d.pct, blender_hash, d.amount_paise, now), true); }

  // 4. expired token
  report("--use-expired-pct",
         ex.authorize(&d.pct, d.cart_hash, d.amount_paise, d.pct.body.exp_ns + 1), true);

  // the legitimate path works exactly once...
  std::printf("\n  %sthe legitimate path%s\n\n", DIM, RST);
  report("valid PCT (first use)",
         ex.authorize(&d.pct, d.cart_hash, d.amount_paise, now), false);

  // 5. ...and replaying it is refused, because the nonce is burned
  report("--replay-last-valid-pct",
         ex.authorize(&d.pct, d.cart_hash, d.amount_paise, now), true);

  std::printf("\n  %sadmission forgeries refused=3 | execution bypasses: authorized=%llu refused=%llu%s\n",
    DIM, (unsigned long long)ex.authorized(), (unsigned long long)ex.refused(), RST);
  if (fails) { std::printf("\n  %s%d ATTACK(S) BEHAVED UNEXPECTEDLY%s\n\n", RED, fails, RST); return 1; }
  std::printf("\n  %sall bypasses refused; exactly one legitimate payment authorized%s\n\n", GRN, RST);
  return 0;
}

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "\n  error: %s\n\n", e.what());
    return 4;
  }
}
