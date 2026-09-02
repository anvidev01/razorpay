// SKU/merchant string interning. Strings die at the parse boundary; everything
// downstream (kernel, WAL, capability token) sees only uint32_t ids.
// This is a CORRECTNESS property, not just a speed one: it is structurally
// impossible for a borrowed string_view to reach the WAL. See docs/04-INCIDENT-2AM.md.
#pragma once
#include <cstdint>
#include <cstring>
#include <string_view>

namespace rig {

class InternTable {
public:
  static constexpr std::uint32_t TSZ      = 4096;   // power of two
  static constexpr std::uint32_t MAX_IDS  = 1024;
  static constexpr std::uint32_t MAX_LEN  = 48;
  static constexpr std::uint32_t INVALID  = 0;

  InternTable() noexcept { std::memset(table_, 0, sizeof(table_)); }

  static std::uint64_t hash(const char* p, std::size_t n) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < n; ++i) {
      h ^= static_cast<unsigned char>(p[i]);
      h *= 1099511628211ull;
    }
    return h | 1ull;   // never 0: 0 marks an empty slot
  }

  // Insert-or-get. Returns INVALID if the table or name arena is full.
  std::uint32_t intern(const char* p, std::size_t n) noexcept {
    if (n == 0 || n > MAX_LEN) return INVALID;
    const std::uint64_t h = hash(p, n);
    std::uint32_t i = static_cast<std::uint32_t>(h) & (TSZ - 1);
    while (table_[i].h) {
      if (table_[i].h == h) return table_[i].id;
      i = (i + 1) & (TSZ - 1);
    }
    if (next_id_ >= MAX_IDS || name_head_ + n > sizeof(names_)) return INVALID;
    const std::uint32_t id = next_id_++;
    name_off_[id] = name_head_;
    name_len_[id] = static_cast<std::uint16_t>(n);
    std::memcpy(names_ + name_head_, p, n);
    name_head_ += static_cast<std::uint32_t>(n);
    table_[i].h  = h;
    table_[i].id = id;
    return id;
  }

  // Lookup without inserting. INVALID if absent.
  std::uint32_t lookup(const char* p, std::size_t n) const noexcept {
    if (n == 0 || n > MAX_LEN) return INVALID;
    const std::uint64_t h = hash(p, n);
    std::uint32_t i = static_cast<std::uint32_t>(h) & (TSZ - 1);
    while (table_[i].h) {
      if (table_[i].h == h) return table_[i].id;
      i = (i + 1) & (TSZ - 1);
    }
    return INVALID;
  }

  std::uint32_t intern(std::string_view s) noexcept { return intern(s.data(), s.size()); }
  std::uint32_t lookup(std::string_view s) const noexcept { return lookup(s.data(), s.size()); }

  // Reverse lookup -- required for an explainable audit trail.
  std::string_view name(std::uint32_t id) const noexcept {
    if (id == INVALID || id >= next_id_) return {};
    return std::string_view(names_ + name_off_[id], name_len_[id]);
  }

  std::uint32_t count() const noexcept { return next_id_ - 1; }

private:
  struct alignas(64) Slot { std::uint64_t h; std::uint32_t id; std::uint32_t _pad; };
  Slot          table_[TSZ];
  char          names_[MAX_IDS * MAX_LEN];
  std::uint32_t name_off_[MAX_IDS] = {};
  std::uint16_t name_len_[MAX_IDS] = {};
  std::uint32_t name_head_ = 0;
  std::uint32_t next_id_   = 1;   // 0 reserved for INVALID
};

}  // namespace rig
