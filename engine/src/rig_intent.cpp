// rig-intent: plain English -> a DRAFT mandate, from the terminal.
//
// The draft is NOT signed. It prints what a human would be asked to approve, and the
// mandate JSON you can hand to rig-eval once you do approve it.
#include "rig/intent.hpp"
#include <simdjson.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>

static const char* G = "\033[32m"; static const char* Y = "\033[33m";
static const char* D = "\033[2m";  static const char* B = "\033[1m";
static const char* Z = "\033[0m";

using simdjson::simdjson_error;

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
      "usage: rig-intent \"buy me two meals and a drink under 1000 rupees\"\n"
      "                 [--out FILE] [--minutes N] [--catalog FILE]\n"
      "\n"
      "  --catalog  point at your own product feed to test another sector.\n"
      "             See fixtures/catalog.json for the shape.\n");
    return 2;
  }
  std::string utterance = argv[1], out_path;
  // The catalogue was hardcoded, so anyone evaluating this on their own sector had
  // to overwrite a fixture in the repo. It is the merchant's product feed, not a
  // property of the engine -- so it is an argument.
  std::string catalog_path = "fixtures/catalog.json";
  int minutes = 30;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) out_path = argv[++i];
    else if (a == "--minutes" && i + 1 < argc) minutes = std::atoi(argv[++i]);
    else if (a == "--catalog" && i + 1 < argc) catalog_path = argv[++i];
  }

  std::ifstream f(catalog_path);
  if (!f) { std::fprintf(stderr, "cannot read %s\n", catalog_path.c_str()); return 2; }
  std::ostringstream cb; cb << f.rdbuf();

  const rig::IntentDraft d = rig::translate_intent(utterance, cb.str());
  std::printf("\n  %s\"%s\"%s\n\n", D, utterance.c_str(), Z);
  if (!d.ok) {
    std::printf("  %sCannot draft a mandate:%s %s\n", Y, Z, d.error.c_str());
    if (!d.unmatched.empty())
      std::printf("  %snot in this catalogue:%s %s\n", Y, Z, d.unmatched.c_str());
    std::printf("\n");
    return 1;
  }
  std::printf("  %sUnderstood%s  %s\n", B, Z, d.interpretation.c_str());
  std::printf("  %sdrafted by%s %s", D, Z, d.model.c_str());
  if (d.input_tokens) std::printf("  %d+%d tokens  %llu ms",
      d.input_tokens, d.output_tokens, (unsigned long long)d.latency_ms);
  std::printf("\n");
  if (!d.unmatched.empty())
    std::printf("  %scannot source%s %s %s(left out, not substituted)%s\n",
                Y, Z, d.unmatched.c_str(), D, Z);
  std::printf("\n  %sThis is a DRAFT. Nothing is signed until a human approves it.%s\n", Y, Z);

  // Turn the draft into a mandate the engine accepts. The draft speaks rupees and
  // human field names; the engine takes integer paise and its own schema. Converting
  // here (rather than hand-editing) is what makes the CLI path usable end to end.
  if (!out_path.empty()) {
    try {
      simdjson::padded_string pj(d.mandate_json);
      simdjson::ondemand::parser parser;
      auto doc = parser.iterate(pj);

      double budget = 0; doc["budget_rupees"].get(budget);
      std::uint64_t mins = static_cast<std::uint64_t>(minutes);
      const std::time_t now = std::time(nullptr);

      std::ostringstream o;
      o << "{\n  \"mandate_id\": \"mnd_cli\",\n";
      o << "  \"utterance\": \"" << utterance << "\",\n";
      o << "  \"not_before_ns\": " << (std::uint64_t(now - 60) * 1000000000ull) << ",\n";
      o << "  \"not_after_ns\": "  << (std::uint64_t(now) + mins * 60) * 1000000000ull << ",\n";
      o << "  \"total_budget_paise\": " << static_cast<long long>(budget * 100 + 0.5) << ",\n";

      o << "  \"merchant_allow\": [";
      bool first = true;
      for (auto m : doc["merchants"].get_array()) {
        std::string_view v; if (m.get_string().get(v) != simdjson::SUCCESS) continue;
        if (!first) o << ", "; first = false;
        o << "\"" << v << "\"";
      }
      o << "],\n";

      std::string_view pol = "same_category";
      (void)doc["substitution_policy"].get_string().get(pol);
      double uplift = 20; doc["substitution_uplift_pct"].get(uplift);
      o << "  \"substitution\": { \"policy\": \"" << pol << "\", \"max_delta_bp\": "
        << static_cast<int>(uplift * 100 + 0.5) << " },\n";

      o << "  \"constraints\": [\n";
      first = true;
      for (auto it : doc["items"].get_array()) {
        std::string_view sku, cat;
        (void)it["sku"].get_string().get(sku);
        const bool has_cat = it["category"].get_string().get(cat) == simdjson::SUCCESS;
        double unit = 0; it["max_unit_rupees"].get(unit);
        std::uint64_t qty = 1; it["max_qty"].get(qty);
        if (!first) o << ",\n"; first = false;
        o << "    { \"sku\": \"" << sku << "\"";
        if (has_cat) o << ", \"category\": \"" << cat << "\"";
        o << ", \"max_unit_paise\": " << static_cast<long long>(unit * 100 + 0.5)
          << ", \"max_qty\": " << qty << " }";
      }
      o << "\n  ],\n  \"schema_version\": 1\n}\n";

      std::ofstream f2(out_path);
      if (!f2) { std::fprintf(stderr, "cannot write %s\n", out_path.c_str()); return 2; }
      f2 << o.str();
      std::printf("\n  %smandate written to%s %s  %svalid %d min%s\n",
                  G, Z, out_path.c_str(), D, minutes, Z);
      std::printf("  %snext:%s ./build/rig-eval %s <cart.json> --execute\n",
                  D, Z, out_path.c_str());
    } catch (const simdjson_error& e) {
      std::fprintf(stderr, "could not convert the draft: %s\n", e.what());
      return 2;
    }
  } else {
    std::printf("  %sre-run with --out draft.json to write a mandate file%s\n", D, Z);
  }
  std::printf("\n");
  return 0;
}
