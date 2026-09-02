// Crypto primitives. Three distinct jobs, three different cost profiles:
//   Ed25519  36.5 us  -- mandate authenticity + capability tokens. ADMISSION PATH ONLY.
//   SHA-256  ~1 us    -- WAL hash chain, cart hashes. Commit path.
//   SipHash  ~50 ns   -- in-memory integrity tag on a cached schema. HOT PATH.
// Putting Ed25519 on the hot path would cost 1302x the policy kernel. See docs/BENCHMARKS.md.
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <array>

namespace rig {

using Hash256 = std::array<std::uint8_t, 32>;
using Sig512  = std::array<std::uint8_t, 64>;
using Tag128  = std::array<std::uint8_t, 16>;

// --- SipHash-2-4: cheap keyed integrity tag, safe for the hot path ---
Tag128 siphash(const void* data, std::size_t len, const std::uint8_t key[16]) noexcept;

// --- SHA-256 (OpenSSL) ---
Hash256 sha256(const void* data, std::size_t len) noexcept;
Hash256 sha256_chain(const Hash256& prev, const void* data, std::size_t len) noexcept;

// --- CRC32C, for per-record corruption detection ---
std::uint32_t crc32c(const void* data, std::size_t len) noexcept;

// --- Ed25519 (OpenSSL) ---
class Signer {
public:
  Signer();                       // generates a fresh keypair
  ~Signer();
  Signer(const Signer&)            = delete;
  Signer& operator=(const Signer&) = delete;

  Sig512 sign(const void* msg, std::size_t len) const;
  bool   verify(const void* msg, std::size_t len, const Sig512& sig) const;
  std::array<std::uint8_t, 32> public_key() const;

private:
  void* pkey_ = nullptr;          // EVP_PKEY*
};

std::string hex(const void* data, std::size_t len);
template <class T> std::string hex(const T& a) { return hex(a.data(), a.size()); }

}  // namespace rig
