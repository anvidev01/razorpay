// Regression tests for the natural-language translator.
//
// This bug class has bitten twice: "mojito" silently became a lime soda, then
// "chicken tikka" silently became a chicken biryani. An unrequested substitution is
// the exact failure the policy kernel exists to catch, so the translator is held to
// the same rule and pinned here.
#include "rig/intent.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace rig;

static int g_fail = 0, g_run = 0;
#define CHECK(cond, msg) do{ ++g_run; if(!(cond)){ ++g_fail; \
  std::printf("  FAIL %-58s (%s:%d)\n", (msg), __FILE__, __LINE__);} }while(0)

static bool has(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1] : "fixtures/catalog.json";
  std::ifstream f(path);
  if (!f) { std::printf("cannot read %s\n", path); return 2; }
  std::ostringstream b; b << f.rdbuf();
  const std::string catalog = b.str();

  std::printf("== intent translator ==\n");

  {  // the reported bug: a qualifier the catalogue cannot honour
    auto d = translate_local(
      "buy me a dosa and chicken tikka with a mojito and a chocolate shake under rupees 1000",
      catalog);
    CHECK(d.ok,                                    "drafts something from a mixed request");
    CHECK(has(d.interpretation, "Masala dosa"),    "serves the dosa that was asked for");
    CHECK(has(d.interpretation, "Virgin mojito"),  "serves the mojito that was asked for");
    CHECK(!has(d.interpretation, "biryani"),       "does NOT swap chicken tikka for biryani");
    CHECK(!has(d.interpretation, "Cold coffee"),   "does NOT swap chocolate shake for cold coffee");
    CHECK(has(d.unmatched, "tikka"),               "reports chicken tikka as unavailable");
    CHECK(has(d.unmatched, "chocolate"),           "reports chocolate shake as unavailable");
    std::printf("   unmatched: %s\n", d.unmatched.c_str());
  }
  {  // the earlier bug, kept pinned
    auto d = translate_local("get me a mojito", catalog);
    CHECK(has(d.interpretation, "Virgin mojito"),  "mojito resolves to the mojito");
    CHECK(!has(d.interpretation, "lime"),          "mojito is not served as a lime soda");
  }
  {  // several named items in one category must all survive
    auto d = translate_local("a pizza, a dosa and a lassi under 800", catalog);
    CHECK(has(d.interpretation, "pizza"),          "keeps the pizza");
    CHECK(has(d.interpretation, "dosa"),           "keeps the dosa (same category as pizza)");
    CHECK(has(d.interpretation, "lassi"),          "keeps the lassi");
  }
  {  // generic words still work, and a named item suppresses the generic one
    auto d = translate_local("two meals and a drink under 1000", catalog);
    CHECK(d.ok && has(d.interpretation, "2 x"),    "generic 'two meals' gives quantity 2");
    CHECK(has(d.interpretation, "under Rs 1000"),  "budget honoured");
  }
  {  // quantities are not budgets
    auto d = translate_local("2 drinks, one shake and one mojito", catalog);
    CHECK(!has(d.interpretation, "under Rs 2"),    "'2 drinks' is a count, not a Rs 2 budget");
    CHECK(has(d.interpretation, "shake") && has(d.interpretation, "mojito"),
                                                   "both named drinks survive");
  }
  {  // spelled-out amounts
    auto d = translate_local("two meals and a drink under the budget of rupees thousand", catalog);
    CHECK(has(d.interpretation, "under Rs 1000"),  "'rupees thousand' parses as 1000");
  }
  {  // nothing in the catalogue -> refuse, never approximate
    auto d = translate_local("order sushi and a beer", catalog);
    CHECK(!d.ok,                                   "refuses a request it cannot fill");
    CHECK(!d.error.empty(),                        "says why it refused");
  }

  std::printf("\n%d/%d checks passed%s\n", g_run - g_fail, g_run,
              g_fail ? "  <-- FAILURES" : "");
  return g_fail ? 1 : 0;
}
