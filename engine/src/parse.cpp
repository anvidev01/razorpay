#include "rig/parse.hpp"
#include <simdjson.h>
#include <cstring>

namespace rig {
using namespace simdjson;

// Mandate ids are opaque strings ("mnd_8f21c4"). Hash to a stable uint64 so the
// engine, WAL and capability token all key on a fixed-width, POD identifier.
static std::uint64_t mandate_key(std::string_view s) noexcept {
  return InternTable::hash(s.data(), s.size());
}

Hash256 canonical_cart_hash(const CartView& c) noexcept {
  // Hash the INTERNED, canonical form -- not the raw JSON. Two carts that differ only
  // in whitespace or key order hash identically; two carts that differ in any
  // money-relevant field do not.
  std::uint8_t buf[4 + MAX_CART * 16 + 4];
  std::size_t  o = 0;
  std::memcpy(buf + o, &c.merchant_id, 4); o += 4;
  const std::uint32_t n = c.n > MAX_CART ? MAX_CART : c.n;
  std::memcpy(buf + o, &n, 4); o += 4;
  for (std::uint32_t i = 0; i < n; ++i) {
    std::memcpy(buf + o, &c.sku_id[i], 4);     o += 4;
    std::memcpy(buf + o, &c.unit_paise[i], 8); o += 8;
    std::memcpy(buf + o, &c.qty[i], 4);        o += 4;
  }
  return sha256(buf, o);
}

ParseResult parse_intent(const std::string& json, InternTable& t, IntentSchema& out) {
  ParseResult r;
  out = IntentSchema{};
  try {
    padded_string p(json);
    ondemand::parser parser;
    auto doc = parser.iterate(p);

    out.schema_version     = SCHEMA_VER;
    out.mandate_id         = mandate_key(doc["mandate_id"].get_string().value());
    out.not_before_ns      = doc["not_before_ns"].get_uint64();
    out.not_after_ns       = doc["not_after_ns"].get_uint64();
    out.total_budget_paise = doc["total_budget_paise"].get_int64();

    for (auto m : doc["merchant_allow"].get_array()) {
      std::string_view sv = m.get_string();
      const std::uint32_t id = t.intern(sv);
      if (id == InternTable::INVALID || id > 64) { r.bits = R_ENGINE_RESOURCE; r.error = "merchant table full"; return r; }
      out.merchant_allow_mask |= (1ull << (id - 1));
    }

    std::uint32_t n = 0;
    for (auto c : doc["constraints"].get_array()) {
      if (n >= MAXC) { r.bits = R_ENGINE_RESOURCE; r.error = "too many constraints"; return r; }
      std::string_view sku = c["sku"].get_string();
      const std::uint32_t id = t.intern(sku);          // string dies here
      if (id == InternTable::INVALID) { r.bits = R_ENGINE_RESOURCE; r.error = "sku table full"; return r; }
      out.sku_id[n]         = id;
      out.max_unit_paise[n] = c["max_unit_paise"].get_int64();
      out.max_qty[n]        = static_cast<std::uint32_t>(c["max_qty"].get_uint64());
      ++n;
    }
    out.n_constraints = n;
    r.ok = true;
    return r;
  } catch (const simdjson_error& e) {
    r.bits  = R_SCHEMA_VERSION;                       // malformed mandate -> deny
    r.error = e.what();
    return r;
  }
}

ParseResult parse_cart(const std::string& json, InternTable& t, ScratchArena& arena,
                       CartView& out, Hash256& cart_hash, std::uint64_t& mandate_id) {
  ParseResult r;
  out = CartView{};
  if (json.size() > MAX_CART_BYTES) {                 // oversize -> deny, never grow
    r.bits = R_ENGINE_RESOURCE;
    r.error = "cart exceeds MAX_CART_BYTES";
    return r;
  }
  auto* sku = arena.alloc<std::uint32_t>(MAX_CART);
  auto* up  = arena.alloc<std::int64_t>(MAX_CART);
  auto* qty = arena.alloc<std::uint32_t>(MAX_CART);
  if (!sku || !up || !qty) {                          // arena exhausted -> deny
    r.bits = R_ENGINE_RESOURCE;
    r.error = "arena exhausted";
    return r;
  }
  try {
    padded_string p(json);
    ondemand::parser parser;
    auto doc = parser.iterate(p);

    mandate_id = mandate_key(doc["mandate_id"].get_string().value());
    std::string_view merch = doc["merchant"].get_string();
    out.merchant_id = t.intern(merch);

    std::uint32_t n = 0;
    for (auto line : doc["lines"].get_array()) {
      if (n >= MAX_CART) { r.bits = R_ENGINE_RESOURCE; r.error = "too many cart lines"; return r; }
      std::string_view s = line["sku"].get_string();
      // Interned immediately. Nothing downstream ever holds a view into parser memory.
      sku[n] = t.intern(s);
      up[n]  = line["unit_paise"].get_int64();
      qty[n] = static_cast<std::uint32_t>(line["qty"].get_uint64());
      ++n;
    }
    out.sku_id = sku; out.unit_paise = up; out.qty = qty; out.n = n;
    cart_hash  = canonical_cart_hash(out);
    r.ok = true;
    return r;
  } catch (const simdjson_error& e) {
    r.bits  = R_SCHEMA_VERSION;                       // malformed cart -> deny
    r.error = e.what();
    return r;
  }
}

}  // namespace rig
