// Indirect prompt-injection telemetry.
//
// HONEST FRAMING, and this matters: the scanner below is NOT the security boundary.
// Heuristic detection of injected instructions is unreliable by construction -- an
// attacker who knows the patterns writes around them. OWASP ranks prompt injection
// the #1 LLM risk precisely because there is no reliable text-level filter.
//
// The ACTUAL defence in this system is structural: a cart is checked against the
// signed intent mandate. Text on a merchant page cannot add a SKU to a mandate the
// user cryptographically signed earlier, so an injected "buy a 5,000-rupee gift card"
// fails on R_SKU_NOT_IN_INTENT whether or not this scanner notices the text.
//
// The scanner exists to make the ATTEMPT visible in the audit log -- so a merchant or
// an investigator can see that an injection was tried, and when. Detection is
// telemetry; intent-binding is the control.
#pragma once
#include "schema.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace rig {

struct InjectionScan {
  std::uint32_t bits    = 0;    // R_INJECTION_SUSPECTED when anything fired
  std::uint32_t hits    = 0;
  const char*   first   = "";   // which pattern fired first, for the audit record
};

// Case-insensitive substring search over ASCII.
inline bool contains_ci(std::string_view hay, std::string_view needle) noexcept {
  if (needle.size() > hay.size()) return false;
  const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };
  for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
    std::size_t j = 0;
    while (j < needle.size() && lower(hay[i + j]) == lower(needle[j])) ++j;
    if (j == needle.size()) return true;
  }
  return false;
}

// Invisible characters are the strongest signal: legitimate product copy has no reason
// to carry zero-width joiners or bidi overrides, and they are exactly how instructions
// get hidden in a DOM that looks clean to a human.
inline bool has_invisible(std::string_view s) noexcept {
  for (std::size_t i = 0; i + 2 < s.size(); ++i) {
    const auto a = static_cast<unsigned char>(s[i]);
    const auto b = static_cast<unsigned char>(s[i + 1]);
    const auto c = static_cast<unsigned char>(s[i + 2]);
    if (a == 0xE2 && b == 0x80 && (c >= 0x8B && c <= 0x8F)) return true;  // U+200B..200F
    if (a == 0xE2 && b == 0x81 && c == 0xA0) return true;                 // U+2060
    if (a == 0xEF && b == 0xBB && c == 0xBF) return true;                 // U+FEFF
  }
  return false;
}

inline InjectionScan scan_text(std::string_view s) noexcept {
  static constexpr const char* kPatterns[] = {
    "ignore previous", "ignore all previous", "disregard the", "disregard all",
    "system prompt", "</system>", "[system]", "new instruction",
    "you must now", "instead, buy", "instead buy", "override the",
    "<!--", "</script", "act as",
  };
  InjectionScan out;
  for (const char* p : kPatterns) {
    if (contains_ci(s, p)) {
      ++out.hits;
      if (out.hits == 1) out.first = p;
    }
  }
  if (has_invisible(s)) {
    ++out.hits;
    if (out.hits == 1) out.first = "invisible unicode (zero-width / bidi)";
  }
  if (out.hits) out.bits = R_INJECTION_SUSPECTED;
  return out;
}

}  // namespace rig
