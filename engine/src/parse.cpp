#include "rig/parse.hpp"
#include "rig/safety.hpp"
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
  // Hash the INTERNED, canonical form -- not the raw JSON. Two carts differing only in
  // whitespace, key order, added display fields, or LINE ORDER hash identically; two
  // carts differing in any money-relevant field do not.
  //
  // Line order is normalised because an agent regenerating a request after a timeout
  // frequently re-emits the same basket in a different order. Treating that as a new
  // cart is exactly how the double charge happens.
  const std::uint32_t n = c.n > MAX_CART ? MAX_CART : c.n;
  std::uint32_t idx[MAX_CART];
  for (std::uint32_t i = 0; i < n; ++i) idx[i] = i;
  // insertion sort on (sku, price, qty); n <= 64 and this is off the kernel path
  for (std::uint32_t i = 1; i < n; ++i) {
    const std::uint32_t k = idx[i];
    std::int32_t j = static_cast<std::int32_t>(i) - 1;
    const auto less = [&](std::uint32_t a, std::uint32_t b) {
      if (c.sku_id[a]     != c.sku_id[b])     return c.sku_id[a]     < c.sku_id[b];
      if (c.unit_paise[a] != c.unit_paise[b]) return c.unit_paise[a] < c.unit_paise[b];
      return c.qty[a] < c.qty[b];
    };
    while (j >= 0 && less(k, idx[j])) { idx[j + 1] = idx[j]; --j; }
    idx[j + 1] = k;
  }

  std::uint8_t buf[4 + 4 + MAX_CART * 16];
  std::size_t  o = 0;
  std::memcpy(buf + o, &c.merchant_id, 4); o += 4;
  std::memcpy(buf + o, &n, 4);             o += 4;
  for (std::uint32_t i = 0; i < n; ++i) {
    const std::uint32_t j = idx[i];
    std::memcpy(buf + o, &c.sku_id[j], 4);     o += 4;
    std::memcpy(buf + o, &c.unit_paise[j], 8); o += 8;
    std::memcpy(buf + o, &c.qty[j], 4);        o += 4;
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

    // Substitution policy. Absent => SUBST_DENY, i.e. exact SKUs only.
    out.subst_policy       = SUBST_DENY;
    out.subst_max_delta_bp = 0;
    ondemand::object sub;
    if (doc["substitution"].get_object().get(sub) == SUCCESS) {
      std::string_view pol;
      if (sub["policy"].get_string().get(pol) == SUCCESS) {
        if      (pol == "same_category") out.subst_policy = SUBST_SAME_CATEGORY;
        else if (pol == "any_in_budget") out.subst_policy = SUBST_ANY_IN_BUDGET;
        else                             out.subst_policy = SUBST_DENY;
      }
      std::uint64_t bp = 0;
      if (sub["max_delta_bp"].get_uint64().get(bp) == SUCCESS && bp <= 65535)
        out.subst_max_delta_bp = static_cast<std::uint16_t>(bp);
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
      std::string_view cat;
      out.category_id[n] = (c["category"].get_string().get(cat) == SUCCESS)
                             ? t.intern(cat) : 0u;     // interned immediately
      ++n;
    }
    // Two rules for the same SKU make the mandate ambiguous, and find_constraint
    // resolves it by list order -- so a looser rule listed second silently overrides a
    // tighter one written first. A human who wrote "at most 1 at Rs100" would get
    // "4 at Rs450" approved. Refuse the mandate instead of guessing: this is an
    // authorisation, and the only safe reading of an ambiguous authorisation is none.
    for (std::uint32_t i = 0; i < n; ++i)
      for (std::uint32_t j = i + 1; j < n; ++j)
        if (out.sku_id[i] == out.sku_id[j]) {
          r.bits  = R_SCHEMA_VERSION;
          r.error = "duplicate rule for '" + std::string(t.name(out.sku_id[i]))
                  + "': two rules for one SKU are ambiguous. Keep the one you meant, "
                    "or give them different SKUs.";
          return r;
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
                       CartView& out, Hash256& cart_hash, std::uint64_t& mandate_id,
                       std::uint64_t* agent_session_id) {
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
  auto* cat = arena.alloc<std::uint32_t>(MAX_CART);
  if (!sku || !up || !qty || !cat) {                  // arena exhausted -> deny
    r.bits = R_ENGINE_RESOURCE;
    r.error = "arena exhausted";
    return r;
  }
  try {
    padded_string p(json);
    ondemand::parser parser;
    auto doc = parser.iterate(p);

    mandate_id = mandate_key(doc["mandate_id"].get_string().value());
    if (agent_session_id) {
      std::string_view sid;
      *agent_session_id = (doc["agent_session_id"].get_string().get(sid) == SUCCESS)
                            ? mandate_key(sid) : 0ull;
    }
    std::string_view merch = doc["merchant"].get_string();
    out.merchant_id = t.intern(merch);

    std::uint32_t n = 0, flags = 0;
    for (auto line : doc["lines"].get_array()) {
      if (n >= MAX_CART) { r.bits = R_ENGINE_RESOURCE; r.error = "too many cart lines"; return r; }
      std::string_view s = line["sku"].get_string();
      // Interned immediately. Nothing downstream ever holds a view into parser memory.
      sku[n] = t.intern(s);
      up[n]  = line["unit_paise"].get_int64();
      qty[n] = static_cast<std::uint32_t>(line["qty"].get_uint64());
      std::string_view cs;
      cat[n] = (line["category"].get_string().get(cs) == SUCCESS) ? t.intern(cs) : 0u;

      // Injection telemetry, taken HERE because this is the last place the text
      // exists -- everything downstream sees only uint32 ids. Detection is
      // telemetry; the control is that an injected item is not in the mandate.
      flags |= scan_text(s).bits;
      std::string_view note;
      if (line["name"].get_string().get(note) == SUCCESS) flags |= scan_text(note).bits;
      if (line["note"].get_string().get(note) == SUCCESS) flags |= scan_text(note).bits;
      ++n;
    }
    std::string_view topnote;
    if (doc["note"].get_string().get(topnote) == SUCCESS) flags |= scan_text(topnote).bits;

    out.sku_id = sku; out.unit_paise = up; out.qty = qty; out.category_id = cat;
    out.n = n; out.text_flags = flags;
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
