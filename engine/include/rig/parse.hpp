// simdjson ingest. Chosen over a hand-rolled scanner by measurement: equal p50,
// BETTER p99 (248ns vs 282ns), and hardened against adversarial input -- which
// matters because this JSON is produced by an LLM. See docs/BENCHMARKS.md.
//
// CRITICAL LIFETIME RULE: SKU strings are interned to uint32_t HERE and never
// propagate. No std::string_view into parser-owned memory escapes this boundary.
// That rule is the permanent fix for docs/04-INCIDENT-2AM.md.
#pragma once
#include "schema.hpp"
#include "intern.hpp"
#include "arena.hpp"
#include "crypto.hpp"
#include <string>
#include <string_view>

namespace rig {

inline constexpr std::size_t MAX_CART_BYTES = 16 * 1024;

struct ParseResult {
  bool          ok   = false;
  std::uint32_t bits = 0;      // Reject bits to OR into the verdict on failure
  std::string   error;
};

ParseResult parse_intent(const std::string& json, InternTable& t, IntentSchema& out);

// Allocates the SoA arrays from `arena`. On success `cart_hash` commits to the
// canonical (interned) cart, which is what the capability token binds to.
ParseResult parse_cart(const std::string& json, InternTable& t, ScratchArena& arena,
                       CartView& out, Hash256& cart_hash, std::uint64_t& mandate_id);

Hash256 canonical_cart_hash(const CartView& c) noexcept;

}  // namespace rig
