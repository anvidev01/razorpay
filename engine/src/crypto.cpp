#include "rig/crypto.hpp"
#include <openssl/evp.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace rig {

// ---------------- SipHash-2-4, 128-bit output (reference construction) ----------------
namespace {
inline std::uint64_t rotl(std::uint64_t x, int b) { return (x << b) | (x >> (64 - b)); }
inline std::uint64_t load64(const std::uint8_t* p) {
  std::uint64_t v; std::memcpy(&v, p, 8); return v;   // little-endian hosts (arm64/x86)
}
#define SIPROUND                                   \
  do {                                             \
    v0 += v1; v1 = rotl(v1, 13); v1 ^= v0; v0 = rotl(v0, 32); \
    v2 += v3; v3 = rotl(v3, 16); v3 ^= v2;         \
    v0 += v3; v3 = rotl(v3, 21); v3 ^= v0;         \
    v2 += v1; v1 = rotl(v1, 17); v1 ^= v2; v2 = rotl(v2, 32); \
  } while (0)
}  // namespace

Tag128 siphash(const void* data, std::size_t len, const std::uint8_t key[16]) noexcept {
  const auto* in = static_cast<const std::uint8_t*>(data);
  const std::uint64_t k0 = load64(key), k1 = load64(key + 8);
  std::uint64_t v0 = k0 ^ 0x736f6d6570736575ull;
  std::uint64_t v1 = k1 ^ 0x646f72616e646f6dull;
  std::uint64_t v2 = k0 ^ 0x6c7967656e657261ull;
  std::uint64_t v3 = k1 ^ 0x7465646279746573ull;
  v1 ^= 0xee;                                        // 128-bit variant

  const std::size_t left = len & 7;
  const std::uint8_t* end = in + len - left;
  for (; in != end; in += 8) {
    const std::uint64_t m = load64(in);
    v3 ^= m; SIPROUND; SIPROUND; v0 ^= m;
  }
  std::uint64_t b = static_cast<std::uint64_t>(len) << 56;
  for (std::size_t i = 0; i < left; ++i) b |= static_cast<std::uint64_t>(in[i]) << (8 * i);
  v3 ^= b; SIPROUND; SIPROUND; v0 ^= b;

  v2 ^= 0xee;
  SIPROUND; SIPROUND; SIPROUND; SIPROUND;
  const std::uint64_t lo = v0 ^ v1 ^ v2 ^ v3;
  v1 ^= 0xdd;
  SIPROUND; SIPROUND; SIPROUND; SIPROUND;
  const std::uint64_t hi = v0 ^ v1 ^ v2 ^ v3;

  Tag128 out{};
  std::memcpy(out.data(), &lo, 8);
  std::memcpy(out.data() + 8, &hi, 8);
  return out;
}
#undef SIPROUND

// ---------------- SHA-256 (EVP; the low-level SHA256_* API is deprecated in OpenSSL 3) ----
Hash256 sha256(const void* data, std::size_t len) noexcept {
  Hash256 out{};
  unsigned int n = 0;
  EVP_Digest(data, len, out.data(), &n, EVP_sha256(), nullptr);
  return out;
}

Hash256 sha256_chain(const Hash256& prev, const void* data, std::size_t len) noexcept {
  Hash256 out{};
  EVP_MD_CTX* c = EVP_MD_CTX_new();
  unsigned int n = 0;
  EVP_DigestInit_ex(c, EVP_sha256(), nullptr);
  EVP_DigestUpdate(c, prev.data(), prev.size());
  EVP_DigestUpdate(c, data, len);
  EVP_DigestFinal_ex(c, out.data(), &n);
  EVP_MD_CTX_free(c);
  return out;
}

// ---------------- CRC32C (Castagnoli, reflected) ----------------
namespace {
struct Crc32cTable {
  std::uint32_t t[256];
  constexpr Crc32cTable() : t{} {
    for (std::uint32_t i = 0; i < 256; ++i) {
      std::uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
      t[i] = c;
    }
  }
};
constexpr Crc32cTable kCrc{};
}  // namespace

std::uint32_t crc32c(const void* data, std::size_t len) noexcept {
  const auto* p = static_cast<const std::uint8_t*>(data);
  std::uint32_t c = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) c = kCrc.t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

// ---------------- Ed25519 ----------------
Signer::Signer() {
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  if (!ctx) throw std::runtime_error("ed25519: ctx");
  EVP_PKEY* k = nullptr;
  if (EVP_PKEY_keygen_init(ctx) <= 0 || EVP_PKEY_keygen(ctx, &k) <= 0) {
    EVP_PKEY_CTX_free(ctx);
    throw std::runtime_error("ed25519: keygen");
  }
  EVP_PKEY_CTX_free(ctx);
  pkey_ = k;
}

Signer::~Signer() { if (pkey_) EVP_PKEY_free(static_cast<EVP_PKEY*>(pkey_)); }

Sig512 Signer::sign(const void* msg, std::size_t len) const {
  EVP_MD_CTX* m = EVP_MD_CTX_new();
  Sig512 sig{};
  std::size_t siglen = sig.size();
  if (EVP_DigestSignInit(m, nullptr, nullptr, nullptr, static_cast<EVP_PKEY*>(pkey_)) <= 0 ||
      EVP_DigestSign(m, sig.data(), &siglen, static_cast<const unsigned char*>(msg), len) <= 0) {
    EVP_MD_CTX_free(m);
    throw std::runtime_error("ed25519: sign");
  }
  EVP_MD_CTX_free(m);
  return sig;
}

bool Signer::verify(const void* msg, std::size_t len, const Sig512& sig) const {
  EVP_MD_CTX* m = EVP_MD_CTX_new();
  if (EVP_DigestVerifyInit(m, nullptr, nullptr, nullptr, static_cast<EVP_PKEY*>(pkey_)) <= 0) {
    EVP_MD_CTX_free(m);
    return false;
  }
  const int r = EVP_DigestVerify(m, sig.data(), sig.size(),
                                 static_cast<const unsigned char*>(msg), len);
  EVP_MD_CTX_free(m);
  return r == 1;
}

std::array<std::uint8_t, 32> Signer::public_key() const {
  std::array<std::uint8_t, 32> pk{};
  std::size_t n = pk.size();
  EVP_PKEY_get_raw_public_key(static_cast<EVP_PKEY*>(pkey_), pk.data(), &n);
  return pk;
}

std::string hex(const void* data, std::size_t len) {
  static const char* d = "0123456789abcdef";
  const auto* p = static_cast<const std::uint8_t*>(data);
  std::string s(len * 2, '0');
  for (std::size_t i = 0; i < len; ++i) { s[2*i] = d[p[i] >> 4]; s[2*i+1] = d[p[i] & 0xF]; }
  return s;
}

}  // namespace rig
