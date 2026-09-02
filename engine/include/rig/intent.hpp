// Natural language -> a DRAFT mandate.
//
// THE ARCHITECTURAL POINT, and it is the whole reason this is safe:
// the model's output is a PROPOSAL, never an authorisation. It is shown to the human,
// who confirms it, and only then is it signed and admitted. The model cannot sign
// anything -- it has no key and no path to /api/admit.
//
// So the safety of the system does NOT depend on the translation being correct. A bad
// translation is caught at the confirmation step, exactly like a bad cart is caught at
// the policy step. The LLM is a convenience; the signature is the authority.
#pragma once
#include <cstdint>
#include <string>

namespace rig {

struct IntentDraft {
  bool        ok = false;
  std::string source;        // "claude" | "local-stub"
  std::string model;         // model id when Claude produced it
  std::string interpretation;// one line shown to the human before they confirm
  std::string mandate_json;  // the DRAFT mandate, unsigned
  std::string error;
  std::string unmatched;     // words the person asked for that this catalogue has no item for
  std::uint64_t latency_ms = 0;
  int         input_tokens  = 0;
  int         output_tokens = 0;
};

// `catalog_json` is the merchant's product feed. `utterance` is what the human typed.
IntentDraft translate_intent(const std::string& utterance, const std::string& catalog_json);

// Deterministic keyword matcher used when no ANTHROPIC_API_KEY is present, so the demo
// runs offline. Labelled "local-stub" everywhere it surfaces -- it is NOT an LLM and
// must never be presented as one.
IntentDraft translate_local(const std::string& utterance, const std::string& catalog_json);

}  // namespace rig
