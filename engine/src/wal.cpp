#include "rig/wal.hpp"
#include "rig/clock.hpp"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <time.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace rig {

const char* rectype_name(RecType t) noexcept {
  switch (t) {
    case RecType::MANDATE_ISSUED:    return "MANDATE_ISSUED";
    case RecType::CART_PROPOSED:     return "CART_PROPOSED";
    case RecType::POLICY_DECISION:   return "POLICY_DECISION";
    case RecType::CAPABILITY_ISSUED: return "CAPABILITY_ISSUED";
    case RecType::CAPABILITY_DENIED: return "CAPABILITY_DENIED";
    case RecType::PAYMENT_ATTEMPTED: return "PAYMENT_ATTEMPTED";
    case RecType::PAYMENT_RESULT:    return "PAYMENT_RESULT";
    case RecType::REMEDIATION:       return "REMEDIATION";
    case RecType::ANCHOR:            return "ANCHOR";
    case RecType::DUPLICATE_SUPPRESSED: return "DUPLICATE_SUPPRESSED";
    case RecType::STEP_UP_REQUIRED:  return "STEP_UP_REQUIRED";
    case RecType::HUMAN_CONFIRMED:   return "HUMAN_CONFIRMED";
    case RecType::REVERSAL_REQUESTED: return "REVERSAL_REQUESTED";
    case RecType::REVERSAL_RESULT:    return "REVERSAL_RESULT";
    default:                         return "UNKNOWN";
  }
}


Wal::Wal(std::string path, std::uint32_t group_size, std::uint64_t group_us)
    : path_(std::move(path)), group_size_(group_size), group_us_(group_us) {
  // RECOVERY. An append-only hash chain must resume from the existing head, or the
  // first record written by a new process claims prev_hash=0 and breaks the chain.
  // A torn tail (partial write at crash time) is truncated away, which is exactly
  // what a real WAL does on restart -- the verified prefix is the surviving log.
  if (::access(path_.c_str(), F_OK) == 0) {   // a fresh log is not a torn one
    const ChainReport rep = wal_scan(path_, [](const WalRecord&) { return true; });
    if (rep.records) {
      head_ = rep.head;
      seq_  = rep.last_seq + 1;
      committed_seq_ = rep.last_seq;
    }
    if (!rep.intact) {
      if (::truncate(path_.c_str(), static_cast<off_t>(rep.good_bytes)) != 0)
        throw std::runtime_error("wal: cannot truncate torn tail of " + path_);
      std::fprintf(stderr,
        "wal: recovered %llu records, discarded torn tail after byte %llu (%s)\n",
        (unsigned long long)rep.records, (unsigned long long)rep.good_bytes,
        rep.detail.c_str());
    }
  }
  fd_ = ::open(path_.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (fd_ < 0) throw std::runtime_error("wal: cannot open " + path_);

  // EXCLUSIVE WRITER. Two processes appending to one hash chain interleave their
  // records and corrupt it irrecoverably -- each holds its own in-memory head_, so
  // both write prev_hash values that the other invalidates. Fail fast and loudly
  // rather than silently destroying the audit log.
  if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
    ::close(fd_); fd_ = -1;
    throw std::runtime_error("wal: " + path_ + " is locked by another process "
                             "(only one writer may hold a hash chain)");
  }
  batch_.reserve(1 << 20);
  batch_opened_us_ = mono_ns() / 1000;
}

Wal::~Wal() {
  if (fd_ >= 0) { commit(); ::close(fd_); }
}

std::uint64_t Wal::append(RecType t, const void* payload, std::size_t n) {
  const std::uint32_t len = static_cast<std::uint32_t>(sizeof(RecHeader) + n + 64);
  const std::size_t   at  = batch_.size();
  batch_.resize(at + len);
  std::uint8_t* rec = batch_.data() + at;

  RecHeader h{};
  h.len     = len;
  h.crc     = 0;
  h.seq     = seq_;
  h.wall_ns = wall_ns();
  h.mono_ns = mono_ns();
  h.type    = static_cast<std::uint8_t>(t);
  h.version = 1;
  h.flags   = 0;
  std::memcpy(rec, &h, sizeof h);
  if (n) std::memcpy(rec + sizeof(RecHeader), payload, n);

  // ORDER MATTERS: the crc lives inside the header, and the header is covered by
  // this_hash. Write the crc FIRST, or the bytes on disk differ from the bytes that
  // were hashed and every chain verification fails.
  const std::uint32_t crc = crc32c(rec + 8, len - 64 - 8);   // [8, len-64): seq..payload
  std::memcpy(rec + 4, &crc, 4);

  // prev_hash, then this_hash over (prev_hash || header+payload)
  std::memcpy(rec + sizeof(RecHeader) + n, head_.data(), 32);
  const Hash256 next = sha256_chain(head_, rec, sizeof(RecHeader) + n);
  std::memcpy(rec + sizeof(RecHeader) + n + 32, next.data(), 32);
  head_ = next;

  ++pending_;
  return seq_++;
}

bool Wal::maybe_commit() {
  const std::uint64_t now_us = mono_ns() / 1000;
  if (pending_ == 0) return false;
  if (pending_ >= group_size_ || (now_us - batch_opened_us_) >= group_us_) {
    commit();
    return true;
  }
  return false;
}

std::uint64_t Wal::commit() {
  if (batch_.empty()) return 0;
  const std::uint64_t t0 = mono_ns();
  const std::uint8_t* p  = batch_.data();
  std::size_t left = batch_.size();
  while (left) {
    const ssize_t w = ::write(fd_, p, left);
    if (w <= 0) throw std::runtime_error("wal: short write");
    p += w; left -= static_cast<std::size_t>(w);
  }
  // THE fence. On macOS fsync() alone does not flush the drive cache; durable_flush
  // uses F_FULLFSYNC there and fdatasync on Linux.
  if (durable_flush(fd_) != 0) { /* no stronger primitive available */ }
  const std::uint64_t us = (mono_ns() - t0) / 1000;

  committed_seq_   = seq_ - 1;
  batch_.clear();
  pending_         = 0;
  batch_opened_us_ = mono_ns() / 1000;
  ++syncs_;
  sync_us_total_ += us;
  return us;
}

ChainReport wal_scan(const std::string& path,
                     const std::function<bool(const WalRecord&)>& cb) {
  ChainReport rep;
  const int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) { rep.intact = false; rep.detail = "cannot open " + path; return rep; }

  Hash256 expect_prev{};
  std::vector<std::uint8_t> buf;
  std::uint64_t off = 0;
  for (;;) {
    RecHeader h{};
    const ssize_t r = ::read(fd, &h, sizeof h);
    if (r == 0) break;
    if (r != (ssize_t)sizeof h) { rep.intact = false; rep.detail = "truncated header"; break; }
    if (h.len < REC_OVERHEAD || h.len > (1u << 22)) {
      rep.intact = false; rep.break_seq = h.seq; rep.detail = "implausible record length"; break;
    }
    const std::size_t nrest = h.len - sizeof(RecHeader);
    buf.resize(nrest);
    if (::read(fd, buf.data(), nrest) != (ssize_t)nrest) {
      rep.intact = false; rep.break_seq = h.seq; rep.detail = "truncated record"; break;
    }
    const std::size_t npay = nrest - 64;

    // rebuild the contiguous record to check crc + chain
    std::vector<std::uint8_t> rec(h.len);
    std::memcpy(rec.data(), &h, sizeof h);
    std::memcpy(rec.data() + sizeof h, buf.data(), nrest);

    const std::uint32_t crc = crc32c(rec.data() + 8, h.len - 64 - 8);
    if (crc != h.crc) {
      rep.intact = false; rep.break_seq = h.seq;
      rep.detail = "crc mismatch (record body altered)";
      break;
    }

    WalRecord out;
    out.hdr = h;
    out.payload.assign(buf.begin(), buf.begin() + npay);
    std::memcpy(out.prev_hash.data(), buf.data() + npay, 32);
    std::memcpy(out.this_hash.data(), buf.data() + npay + 32, 32);

    if (out.prev_hash != expect_prev) {
      rep.intact = false; rep.break_seq = h.seq;
      rep.detail = "chain break: prev_hash does not match preceding record";
      break;
    }
    const Hash256 recomputed = sha256_chain(out.prev_hash, rec.data(), sizeof(RecHeader) + npay);
    if (recomputed != out.this_hash) {
      rep.intact = false; rep.break_seq = h.seq;
      rep.detail = "chain break: this_hash does not match record contents";
      break;
    }
    expect_prev     = out.this_hash;
    off            += h.len;
    rep.good_bytes  = off;
    rep.last_seq    = h.seq;
    rep.head        = out.this_hash;
    ++rep.records;
    if (!cb(out)) break;
  }
  ::close(fd);
  return rep;
}

}  // namespace rig
