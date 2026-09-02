// Dispute evidence pack, shared by the CLI (rig-evidence) and the audit UI.
#pragma once
#include <cstdint>
#include <string>

namespace rig {

struct EvidenceReport {
  bool        found       = false;
  bool        reproduces  = false;
  bool        chain_intact= false;
  std::string json;
};

EvidenceReport evidence_json(const std::string& wal_path, std::uint64_t decision_seq);

}  // namespace rig
