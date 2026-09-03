#include "rig/intent.hpp"
#include "rig/clock.hpp"
#include <simdjson.h>
#include <curl/curl.h>
#include <time.h>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <vector>
#include <algorithm>
#include <sstream>

namespace rig {
using namespace simdjson;

// simdjson marks .get() warn_unused_result. Where a field is genuinely optional the
// error is the answer ("absent"), so those call sites cast to void deliberately --
// an unmarked ignore would look like an oversight.

static std::uint64_t mono_ms() { return rig::mono_ns() / 1000000; }

static std::size_t sink(void* p, std::size_t sz, std::size_t n, void* ud) {
  static_cast<std::string*>(ud)->append(static_cast<char*>(p), sz * n);
  return sz * n;
}

static std::string json_escape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) o += ' ';
        else o += c;
    }
  }
  return o;
}

// ---------------------------------------------------------------------------
// The schema the model must fill. Enforced server-side by output_config.format,
// so we get valid JSON or an error -- never prose we have to regex.
// ---------------------------------------------------------------------------
static const char* kSchema = R"JSON({
  "type":"object",
  "properties":{
    "interpretation":{"type":"string","description":"One plain sentence restating what the human asked for, shown to them before they sign."},
    "budget_rupees":{"type":"number"},
    "valid_minutes":{"type":"integer"},
    "merchants":{"type":"array","items":{"type":"string"}},
    "substitution_policy":{"type":"string","enum":["deny","same_category","any_in_budget"]},
    "substitution_uplift_pct":{"type":"number"},
    "items":{"type":"array","items":{
      "type":"object",
      "properties":{
        "sku":{"type":"string"},
        "category":{"type":"string"},
        "max_unit_rupees":{"type":"number"},
        "max_qty":{"type":"integer"}
      },
      "required":["sku","category","max_unit_rupees","max_qty"],
      "additionalProperties":false}}
  },
  "required":["interpretation","budget_rupees","valid_minutes","merchants","substitution_policy","substitution_uplift_pct","items"],
  "additionalProperties":false
})JSON";

static const char* kSystem =
  "You convert a person's shopping request into a spending mandate for an AI agent.\n"
  "Use ONLY SKUs from the catalogue provided. Never invent a SKU.\n"
  "max_unit_rupees is a CEILING per unit -- set it at or slightly above the catalogue "
  "price, never below, or the purchase will be refused.\n"
  "max_qty is how many of that item the person asked for.\n"
  "If they state a total budget, use it. If not, set budget_rupees to roughly the sum "
  "of ceilings plus 15% headroom.\n"
  "Default valid_minutes to 30, substitution_policy to same_category, and "
  "substitution_uplift_pct to 20 unless they say otherwise.\n"
  "Default merchants to every merchant in the catalogue unless they name one.\n"
  "Be conservative: a mandate that is too tight gets refused and retried, but one that "
  "is too loose lets an agent overspend. The human reviews this before signing.";

// ---------------------------------------------------------------------------
// Claude path
// ---------------------------------------------------------------------------
static IntentDraft translate_claude(const std::string& utterance,
                                    const std::string& catalog_json,
                                    const char* api_key) {
  IntentDraft d;
  d.source = "claude";
  d.model  = "claude-opus-5";
  const std::uint64_t t0 = mono_ms();

  std::ostringstream body;
  body << R"({"model":"claude-opus-5","max_tokens":16000,)"
       << R"("system":")" << json_escape(kSystem) << R"(",)"
       // Simple extraction: low effort keeps it fast and cheap. Thinking is on by
       // default on Opus 5; we do not disable it (that has known failure modes).
       << R"("output_config":{"effort":"low","format":{"type":"json_schema","schema":)"
       << kSchema << R"(}},)"
       << R"("messages":[{"role":"user","content":")"
       << json_escape("Catalogue:\n" + catalog_json + "\n\nThe person said: \"" + utterance + "\"")
       << R"("}]})";
  const std::string payload = body.str();

  CURL* c = curl_easy_init();
  if (!c) { d.error = "curl init failed"; return d; }
  std::string resp;
  curl_slist* h = nullptr;
  h = curl_slist_append(h, "content-type: application/json");
  h = curl_slist_append(h, "anthropic-version: 2023-06-01");
  const std::string keyhdr = std::string("x-api-key: ") + api_key;   // never logged
  h = curl_slist_append(h, keyhdr.c_str());

  curl_easy_setopt(c, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
  curl_easy_setopt(c, CURLOPT_POST, 1L);
  curl_easy_setopt(c, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)payload.size());
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 60000L);

  const CURLcode rc = curl_easy_perform(c);
  long status = 0;
  curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
  curl_slist_free_all(h);
  curl_easy_cleanup(c);
  d.latency_ms = mono_ms() - t0;

  if (rc != CURLE_OK) { d.error = curl_easy_strerror(rc); return d; }

  try {
    padded_string p(resp);
    ondemand::parser parser;
    auto doc = parser.iterate(p);
    if (status != 200) {
      std::string_view m;
      d.error = (doc["error"]["message"].get_string().get(m) == SUCCESS)
                  ? std::string(m) : ("http " + std::to_string(status));
      return d;
    }
    std::uint64_t it = 0, ot = 0;
    doc["usage"]["input_tokens"].get(it);
    doc["usage"]["output_tokens"].get(ot);
    d.input_tokens  = static_cast<int>(it);
    d.output_tokens = static_cast<int>(ot);

    // Structured output arrives as JSON text inside the first text block.
    std::string inner;
    for (auto blk : doc["content"].get_array()) {
      std::string_view ty;
      if (blk["type"].get_string().get(ty) != SUCCESS || ty != "text") continue;
      std::string_view tx;
      if (blk["text"].get_string().get(tx) == SUCCESS) { inner.assign(tx); break; }
    }
    if (inner.empty()) { d.error = "no text block in response"; return d; }

    padded_string ip(inner);
    ondemand::parser ip2;
    auto idoc = ip2.iterate(ip);
    std::string_view interp;
    (void)idoc["interpretation"].get_string().get(interp);
    d.interpretation.assign(interp);
    d.mandate_json = inner;
    d.ok = true;
    return d;
  } catch (const simdjson_error& e) {
    d.error = std::string("could not parse response: ") + e.what();
    return d;
  }
}

// ---------------------------------------------------------------------------
// Offline fallback. A keyword matcher, NOT a language model. Labelled as such.
// ---------------------------------------------------------------------------
static int word_number(const std::string& s) {
  // ORDER MATTERS: this is a substring search, so compounds must precede their
  // parts or "sixteen" matches "six" and returns 6.
  static const struct { const char* w; int n; } k[] = {
    {"eleven",11},{"twelve",12},{"thirteen",13},{"fourteen",14},{"fifteen",15},
    {"sixteen",16},{"seventeen",17},{"eighteen",18},{"nineteen",19},{"twenty",20},
    {"one",1},{"two",2},{"three",3},{"four",4},{"five",5},
    {"six",6},{"seven",7},{"eight",8},{"nine",9},{"ten",10},{"a ",1},{"an ",1}};
  for (auto& e : k) if (s.find(e.w) != std::string::npos) return e.n;
  return 0;
}

// Reads the digit RUN nearest the end of a window, not a single character.
// "10 thalis" and "12 thousand" both used to lose their leading digit because the
// scan stopped at the first digit it touched: 10 -> 1, and 12 thousand -> 2000.
// Returns 0 when the window ends in something other than digits.
static int trailing_number(const std::string& w) noexcept {
  std::size_t e = w.size();
  while (e > 0 && std::isspace(static_cast<unsigned char>(w[e-1]))) --e;
  std::size_t b = e;
  while (b > 0 && std::isdigit(static_cast<unsigned char>(w[b-1]))) --b;
  if (b == e) return 0;
  return std::atoi(w.substr(b, e - b).c_str());
}

IntentDraft translate_local(const std::string& utterance, const std::string& catalog_json) {
  IntentDraft d;
  d.source = "local-stub";
  d.model  = "keyword matcher (no LLM)";
  const std::uint64_t t0 = mono_ms();

  std::string low = utterance;
  std::transform(low.begin(), low.end(), low.begin(), ::tolower);

  // budget: first number >= 50 that follows a currency-ish cue, else largest number
  double budget = 0;
  {
    std::vector<double> nums;
    for (std::size_t i = 0; i < low.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(low[i]))) continue;
      std::size_t j = i;
      while (j < low.size() && (std::isdigit(static_cast<unsigned char>(low[j])) || low[j] == ',')) ++j;
      std::string n = low.substr(i, j - i);
      n.erase(std::remove(n.begin(), n.end(), ','), n.end());
      nums.push_back(std::atof(n.c_str()));
      i = j;
    }
    // "2 drinks" is a quantity, not a budget. Only amounts that could plausibly be
    // money count; anything smaller is a count and must not become the cap.
    for (double v : nums) if (v >= 50) budget = std::max(budget, v);

    // Spelled-out amounts: "rupees thousand", "five hundred", "two thousand".
    // Without this, "under the budget of rupees thousand" silently loses the budget --
    // which is exactly the kind of miss the human confirmation step exists to catch.
    const auto scale = [&](const char* word, double mult) {
      const auto at = low.find(word);
      if (at == std::string::npos) return;
      double lead = 1;
      const std::string before = low.substr(at > 14 ? at - 14 : 0, at > 14 ? 14 : at);
      if (const int w = word_number(before)) lead = w;
      else if (const int n = trailing_number(before)) lead = n;
      budget = std::max(budget, lead * mult);
    };
    scale("thousand", 1000.0);
    scale("hundred", 100.0);
  }

  struct Pick { std::string sku, cat, name; double price; int qty;
                std::size_t pos; bool specific; };
  std::vector<Pick> picks;
  std::vector<Pick> all_items;   // every catalogue entry, for expanding "variety"
  try {
    padded_string p(catalog_json);
    ondemand::parser parser;
    auto doc = parser.iterate(p);
    for (auto it : doc["items"].get_array()) {
      std::string_view sku, name, cat;
      (void)it["sku"].get_string().get(sku);
      (void)it["name"].get_string().get(name);
      (void)it["category"].get_string().get(cat);
      double price = 0; it["price_rupees"].get(price);

      std::size_t hit = std::string::npos;
      for (auto kw : it["keywords"].get_array()) {
        std::string_view k;
        if (kw.get_string().get(k) != SUCCESS) continue;
        const auto at = low.find(std::string(k));
        if (at != std::string::npos && at < hit) hit = at;
      }
      all_items.push_back({std::string(sku), std::string(cat), std::string(name),
                           price, 1, 0, false});
      if (hit == std::string::npos) continue;
      // How specific was the match? A hit on "biryani" should beat a hit on the
      // generic "meal", so a distinctive word wins its category.
      bool specific = false;
      {
        std::string nl(name);
        std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
        std::istringstream ws(nl);
        for (std::string w; ws >> w;)
          if (w.size() > 3 && low.find(w) != std::string::npos) specific = true;
      }
      // quantity: a number word just before the keyword
      const std::string before = low.substr(hit > 12 ? hit - 12 : 0,
                                            hit > 12 ? 12 : hit);
      int q = word_number(before);
      if (!q) {
        // Nearest digit run, whole. Values >= 50 are money by this file's own
        // convention (see the budget scan above), so they are not a count.
        const int n = trailing_number(before);
        if (n > 0 && n < 50) q = n;
      }
      picks.push_back({std::string(sku), std::string(cat), std::string(name), price,
                       q ? q : 1, hit, specific});
    }
  } catch (const simdjson_error& e) {
    d.error = e.what();
    return d;
  }
  // Anything the person named that this catalogue cannot satisfy must be REPORTED,
  // not quietly swapped. An enumerated food-word list cannot work here -- it silently
  // approves every dish the list forgot ("chicken tikka" became "Chicken biryani"
  // because "chicken" matched and "tikka" was invisible). So instead: any word the
  // person typed that appears NOWHERE in this catalogue is unknown, and if it sits
  // next to a matched word it QUALIFIES that match into a different dish, so the
  // match is dropped rather than substituted.
  {
    // vocabulary this merchant can actually talk about
    std::vector<std::string> vocab;
    try {
      padded_string p2(catalog_json);
      ondemand::parser vp;
      auto vdoc = vp.iterate(p2);
      for (auto it : vdoc["items"].get_array()) {
        std::string_view nm;
        (void)it["name"].get_string().get(nm);
        std::string nl(nm);
        std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
        std::istringstream ws(nl);
        for (std::string w; ws >> w;) if (w.size() > 2) vocab.push_back(w);
        for (auto kw : it["keywords"].get_array()) {
          std::string_view k;
          if (kw.get_string().get(k) != SUCCESS) continue;
          std::istringstream ks{std::string(k)};
          for (std::string w; ks >> w;) if (w.size() > 2) vocab.push_back(w);
        }
      }
    } catch (const simdjson_error&) { /* vocabulary stays empty; nothing is flagged */ }

    static const char* kStop[] = {
      "buy","get","order","please","want","need","some","the","and","with","for","under",
      "over","below","within","budget","total","rupees","rupee","each","one","two","three",
      "four","five","six","seven","eight","nine","ten",
      "eleven","twelve","thirteen","fourteen","fifteen","sixteen","seventeen",
      "eighteen","nineteen","twenty","different","category","categories",
      "should","other","also","plus","from","that","this","them","they","have","has","cost",
      "less","than","than","around","about","maximum","max","limit","spend","upto","only",
      "make","sure","would","like","could","give","bring","send","thousand","hundred"};

    // tokenise, keeping word positions so we can test adjacency
    std::vector<std::string> words;
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < low.size();) {
      if (!std::isalpha(static_cast<unsigned char>(low[i]))) { ++i; continue; }
      const std::size_t b = i;
      while (i < low.size() && std::isalpha(static_cast<unsigned char>(low[i]))) ++i;
      words.push_back(low.substr(b, i - b));
      starts.push_back(b);
    }
    const auto word_index_of_char = [&](std::size_t cpos) -> long {
      for (std::size_t w = 0; w < starts.size(); ++w)
        if (starts[w] >= cpos) return static_cast<long>(w);
      return static_cast<long>(words.size());
    };

    std::vector<std::string> unknown;
    std::vector<long>        unknown_at;
    for (std::size_t w = 0; w < words.size(); ++w) {
      const std::string& t = words[w];
      if (t.size() < 3) continue;
      bool stop = false;
      for (const char* sw : kStop) if (t == sw) { stop = true; break; }
      if (stop) continue;
      bool known = false;
      for (const auto& v : vocab)
        if (v.find(t) != std::string::npos || t.find(v) != std::string::npos) { known = true; break; }
      if (!known) { unknown.push_back(t); unknown_at.push_back(static_cast<long>(w)); }
    }

    // Group runs of unknown words into phrases: "suji halwa" is one dish, not two
    // separate mysteries.
    struct Phrase { std::string text; long first, last; };
    std::vector<Phrase> phrases;
    for (std::size_t u = 0; u < unknown.size(); ++u) {
      if (!phrases.empty() && unknown_at[u] == phrases.back().last + 1) {
        phrases.back().text += " " + unknown[u];
        phrases.back().last  = unknown_at[u];
      } else {
        phrases.push_back({unknown[u], unknown_at[u], unknown_at[u]});
      }
    }

    // A phrase only QUALIFIES a match when it is IMMEDIATELY next to it -- "chicken
    // tikka", "chocolate shake". Anything further apart is a separate request:
    // in "suji halwa with mojito", halwa does not make the mojito unavailable.
    std::vector<std::string> refused;
    for (const auto& ph : phrases) {
      bool qualified = false;
      for (auto it = picks.begin(); it != picks.end();) {
        const long pw = word_index_of_char(it->pos);
        if (pw == ph.first - 1 || pw == ph.last + 1) {
          const std::string& hit = words[static_cast<std::size_t>(pw)];
          refused.push_back(pw < ph.first ? hit + " " + ph.text : ph.text + " " + hit);
          it = picks.erase(it);
          qualified = true;
        } else ++it;
      }
      if (!qualified) refused.push_back(ph.text);
    }
    for (std::size_t i = 0; i < refused.size(); ++i)
      d.unmatched += (i ? ", " : "") + refused[i];
  }

  if (picks.empty()) {
    d.error = "nothing in this merchant's catalogue matches that request";
    return d;
  }

  // If a category has a NAMED item ("mojito", "shake"), drop the generic match in
  // that same category. Asking for "2 drinks, a shake and a mojito" must not also
  // add a lime soda because the word "drink" appeared.
  {
    std::vector<std::string> specific_cats;
    for (auto& p : picks)
      if (p.specific) specific_cats.push_back(p.cat);
    picks.erase(std::remove_if(picks.begin(), picks.end(), [&](const Pick& p) {
      return !p.specific && std::find(specific_cats.begin(), specific_cats.end(), p.cat)
                            != specific_cats.end();
    }), picks.end());
  }

  // "3 meals of DIFFERENT category each" / "a variety" means keep the distinct
  // matches rather than collapsing them.
  const bool wants_variety =
      low.find("different") != std::string::npos ||
      low.find("variety")   != std::string::npos ||
      low.find("assorted")  != std::string::npos ||
      low.find(" each")     != std::string::npos;

  // Otherwise collapse to ONE item per category: "two meals and a drink" means two of
  // some main and one of some drink -- not two of every main on the menu. A specific
  // word ("biryani") wins its category; otherwise the cheapest option does.
  // Every item the person NAMED is kept, even several in one category -- "a pizza,
  // a dosa and a lassi" is three items, not one main and one drink. Only generic
  // matches ("meal", "drink") collapse to a single representative per category.
  std::vector<Pick> uniq;
  for (auto& p : picks) {
    if (p.specific) { uniq.push_back(p); continue; }
    auto f = std::find_if(uniq.begin(), uniq.end(),
                          [&](const Pick& u) { return u.cat == p.cat && !u.specific; });
    if (f == uniq.end()) { uniq.push_back(p); continue; }
    const bool better = (p.specific && !f->specific) ||
                        (p.specific == f->specific && p.price < f->price);
    const int q = std::max(f->qty, p.qty);
    if (better) { *f = p; }
    f->qty = q;
  }
  std::sort(uniq.begin(), uniq.end(),
            [](const Pick& a, const Pick& b) { return a.pos < b.pos; });

  // "3 meals of DIFFERENT category each" asks for three DISTINCT items, not three of
  // one. Without this the generic collapse above answers with 3 x the same dish, which
  // is what a user actually hit. Expand a generic pick of quantity N into N distinct
  // catalogue items sharing its category, cheapest first.
  if (wants_variety) {
    std::vector<Pick> expanded;
    for (const auto& p : uniq) {
      if (p.specific || p.qty < 2) { expanded.push_back(p); continue; }
      std::vector<Pick> pool;
      for (const auto& c : all_items) if (c.cat == p.cat) pool.push_back(c);
      std::sort(pool.begin(), pool.end(),
                [](const Pick& a, const Pick& b) { return a.price < b.price; });
      if (static_cast<int>(pool.size()) < p.qty) { expanded.push_back(p); continue; }
      for (int i = 0; i < p.qty; ++i) {
        Pick one = pool[static_cast<std::size_t>(i)];
        one.qty = 1;
        one.pos = p.pos;
        expanded.push_back(one);
      }
    }
    uniq.swap(expanded);
  }

  double ceilsum = 0;
  std::ostringstream items, interp;
  for (std::size_t i = 0; i < uniq.size(); ++i) {
    const double cap = uniq[i].price * 1.10;      // 10% headroom over list price
    ceilsum += cap * uniq[i].qty;
    if (i) { items << ","; interp << ", "; }
    items << "{\"sku\":\"" << uniq[i].sku << "\",\"category\":\"" << uniq[i].cat
          << "\",\"max_unit_rupees\":" << (long)(cap + 0.5)
          << ",\"max_qty\":" << uniq[i].qty << "}";
    interp << uniq[i].qty << " x " << uniq[i].name << " up to Rs "
           << (long)(cap + 0.5) << " each";
  }
  if (budget <= 0) budget = (long)(ceilsum * 1.15 + 0.5);

  std::ostringstream o;
  o << "{\"interpretation\":\"" << json_escape(interp.str())
    << ", total under Rs " << (long)budget << "\","
    << "\"budget_rupees\":" << (long)budget << ",\"valid_minutes\":30,"
    << "\"merchants\":[\"swiggy\",\"zomato\",\"bigbasket\",\"zepto\"],"
    << "\"substitution_policy\":\"same_category\",\"substitution_uplift_pct\":20,"
    << "\"items\":[" << items.str() << "]}";

  d.mandate_json   = o.str();
  d.interpretation = interp.str() + ", total under Rs " + std::to_string((long)budget);
  d.latency_ms     = mono_ms() - t0;
  d.ok             = true;
  return d;
}

IntentDraft translate_intent(const std::string& utterance, const std::string& catalog_json) {
  const char* key = std::getenv("ANTHROPIC_API_KEY");
  if (key && *key) {
    IntentDraft d = translate_claude(utterance, catalog_json, key);
    if (d.ok) return d;
    // Falling back is fine because a draft is not an authorisation -- but say so.
    IntentDraft f = translate_local(utterance, catalog_json);
    f.error = "claude unavailable (" + d.error + "); used the offline matcher";
    return f;
  }
  return translate_local(utterance, catalog_json);
}

}  // namespace rig
