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
  {  // an unknown dish must not take a VALID neighbour down with it.
     // "suji halwa with mojito": halwa is two words from mojito, so the mojito stands.
    auto d = translate_local(
      "buy me a chicken tikka and a suji halwa with mojito and lassi under rupees 1000",
      catalog);
    CHECK(d.ok,                                    "still drafts from the servable part");
    CHECK(has(d.interpretation, "Virgin mojito"),  "mojito survives a nearby unknown dish");
    CHECK(has(d.interpretation, "Sweet lassi"),    "lassi survives too");
    CHECK(!has(d.interpretation, "biryani"),       "chicken tikka is not swapped for biryani");
    CHECK(has(d.unmatched, "chicken tikka"),       "reports 'chicken tikka' as one phrase");
    CHECK(has(d.unmatched, "suji halwa"),          "reports 'suji halwa' as one phrase");
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
  {  // multi-digit quantities. The scan used to stop at the first digit it touched,
     // so "10 meals" silently became 1 -- a mandate far tighter than the human asked
     // for, which then denied their own cart and looked like an engine fault.
    auto d = translate_local("10 meals under 8000 rupees", catalog);
    CHECK(d.ok && has(d.interpretation, "10 x"),   "'10 meals' is ten, not one");
    auto e = translate_local("12 meals under 8000 rupees", catalog);
    CHECK(e.ok && has(e.interpretation, "12 x"),   "'12 meals' is twelve, not one");
  }
  {  // teens, and the substring trap: find("six") hits inside "sixteen"
    auto d = translate_local("sixteen meals under 5000 rupees", catalog);
    CHECK(d.ok && has(d.interpretation, "16 x"),   "'sixteen' is 16, not 6");
    auto e = translate_local("eighteen meals under 6000 rupees", catalog);
    CHECK(e.ok && has(e.interpretation, "18 x"),   "'eighteen' is 18, not 8");
    auto f = translate_local("twenty meals under 6000 rupees", catalog);
    CHECK(f.ok && has(f.interpretation, "20 x"),   "'twenty' is 20");
  }
  {  // the same first-digit bug in the scale parser: "twelve hundred" was Rs 100
    auto d = translate_local("a meal under twelve hundred rupees", catalog);
    CHECK(has(d.interpretation, "under Rs 1200"),  "'twelve hundred' parses as 1200");
  }
  {  // LAKH AND CRORE. This is an Indian payments product and these are the units
     // people actually write money in. "under 1 lakh rupees" understood neither the
     // unit nor the amount: the budget fell back to the sum of item ceilings, so a
     // Rs 1,00,000 request silently became a Rs 304 mandate.
    auto a = translate_local("a meal under 1 lakh rupees", catalog);
    CHECK(has(a.interpretation, "under Rs 100000"),  "'1 lakh' is 100000");
    auto b = translate_local("a meal under 2 lakh rupees", catalog);
    CHECK(has(b.interpretation, "under Rs 200000"),  "'2 lakh' is 200000");
    auto c = translate_local("a meal under 1 crore rupees", catalog);
    CHECK(has(c.interpretation, "under Rs 10000000"),"'1 crore' is 10000000");
    auto d = translate_local("a meal under two lakh rupees", catalog);
    CHECK(has(d.interpretation, "under Rs 200000"),  "'two lakh' works spelled out");
    auto e = translate_local("a meal under 1.5 lakh rupees", catalog);
    CHECK(has(e.interpretation, "under Rs 150000"),  "'1.5 lakh' is 150000, not 5 lakh");
    // and the unit itself is a budget word, never a product
    CHECK(a.unmatched.find("lakh") == std::string::npos,
                                                     "'lakh' is not read as a product");
  }
  {  // the older units must not regress now that larger ones are checked first
    auto a = translate_local("a meal under 5 thousand rupees", catalog);
    CHECK(has(a.interpretation, "under Rs 5000"),    "'5 thousand' still 5000");
    auto b = translate_local("a meal under 500 rupees", catalog);
    CHECK(has(b.interpretation, "under Rs 500"),     "plain numbers still work");
  }
  {  // a large number next to an item must stay money, not become a count
    auto d = translate_local("a meal under 800 rupees", catalog);
    CHECK(d.ok && !has(d.interpretation, "800 x"), "'800 rupees' is not a quantity");
  }
  {  // A MISSPELLED FILLER WORD must not poison the product beside it.
     // "3 drinks undeer 300" refused outright: "undeer" was an unknown word next to
     // "drinks", and an adjacent unknown is read as a qualifier (the rule that stops
     // "chicken tikka" matching chicken biryani), which erased the match.
    auto d = translate_local("order me 3 drinks undeer 300 rupees", catalog);
    CHECK(d.ok,                                    "a typo in a filler word still drafts");
    CHECK(has(d.interpretation, "3 x"),            "quantity survives the typo");
    CHECK(has(d.interpretation, "under Rs 300"),   "budget survives the typo");
  }
  {  // THE LINE THAT MUST NOT MOVE: typo tolerance stops at filler words.
     // Guessing which PRODUCT someone meant is how "mojito" once became a lime soda.
    auto a = translate_local("order me a mojtio under 300 rupees", catalog);
    CHECK(!a.ok,                                   "a misspelled PRODUCT is refused, not guessed");
    CHECK(has(a.suggestion, "mojito"),             "but it says what you probably meant");
    CHECK(!has(a.interpretation, "Fresh lime"),    "and never substitutes a different drink");
    auto b = translate_local("order me a byriani under 500 rupees", catalog);
    CHECK(!b.ok,                                   "two-edit product typo also refused");
    auto c = translate_local("order sushi and a beer", catalog);
    CHECK(!c.ok && c.suggestion.empty(),           "no hint invented for genuinely absent items");
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
