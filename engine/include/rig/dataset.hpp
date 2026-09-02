// Synthetic agent-behaviour dataset for evaluating the Track 02 detector.
//
// WHY SYNTHETIC, STATED UP FRONT: there is no public corpus of labelled agentic-payment
// fraud -- the rails are months old. So the generator below is the honest alternative:
// a documented, seeded behaviour model whose assumptions can be argued with, rather
// than a hand-picked set of examples that flatters the detector.
//
// THE RULE THAT MAKES THE METRICS MEAN ANYTHING: sessions are split into train and
// test by a hash of the session id, so every transaction of a session lands on the
// same side. Thresholds are swept on TRAIN ONLY. Reported numbers are from TEST,
// which the tuning never saw.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rig {

struct Txn {
  std::uint64_t session_id;
  std::uint64_t at_ns;
  std::uint32_t merchant_id;     // 1..64
  std::int64_t  amount_paise;
  int           local_hour;
  bool          anomalous;       // ground truth
  const char*   label;           // why, for error analysis
  bool          in_test;         // held out from tuning
};

struct Dataset {
  std::vector<Txn> txns;
  std::uint32_t    sessions      = 0;
  std::uint32_t    train_txns    = 0;
  std::uint32_t    test_txns     = 0;
  std::uint32_t    train_anom    = 0;
  std::uint32_t    test_anom     = 0;
};

// Deterministic for a given seed: the same seed reproduces the same dataset on any
// machine, so the reported numbers are checkable rather than asserted.
Dataset generate_dataset(std::uint64_t seed, std::uint32_t n_sessions);

// Writes the whole labelled set so a reviewer can inspect or re-score it themselves.
bool write_dataset_csv(const Dataset& d, const std::string& path);

}  // namespace rig
