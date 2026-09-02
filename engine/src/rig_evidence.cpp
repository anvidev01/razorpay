// rig-evidence: CLI wrapper over rig::evidence_json.
#include "rig/evidence.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: rig-evidence <wal> <decision_seq>\n");
    return 2;
  }
  try {
    const auto r = rig::evidence_json(argv[1], std::strtoull(argv[2], nullptr, 10));
    std::printf("%s", r.json.c_str());
    if (!r.found) return 3;
    return (r.reproduces && r.chain_intact) ? 0 : 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "\n  error: %s\n\n", e.what());
    return 4;
  }
}
