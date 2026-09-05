# Mandate Engine

[![verify](https://github.com/anvidev01/razorpay-mandate-engine/actions/workflows/verify.yml/badge.svg)](https://github.com/anvidev01/razorpay-mandate-engine/actions/workflows/verify.yml)

**An AI payment security layer that sits between an AI buyer and the payment rail, so
every money action an agent takes is explainable, bounded and gated — and provable
afterwards.**

> **Windows evaluators — please use WSL2 (Ubuntu or Debian), not native Windows.**
> The durability and audit guarantees are built on POSIX primitives with no MSVC
> equivalent — `fcntl(F_FULLFSYNC)` for the drive-cache flush, `flock()` for the
> single-writer hash chain, and `clock_gettime` for the monotonic clock — so a native
> MSVC build will not compile. Inside WSL2 the Linux instructions below work unchanged,
> and CI proves them on Ubuntu and macOS on every push.

| | |
|---|---|
| **Track** | 01 — AI Growth & Agentic Commerce *(secondary: 02 — AI Risk Manager)* |
| **Project** | Mandate Engine |
| **What it solves** | An AI agent can spend money the human never authorised, because every payment rail checks *who is asking* and *how much* — and nothing checks *what is in the cart*. This is the layer that reads the cart. |
| **Runs on** | Linux, macOS, WSL2 · one command · CI on Ubuntu + macOS |
| **5-min walkthrough** | ⚠️ **PASTE THE UNLISTED VIDEO URL HERE BEFORE SUBMITTING** — script: [docs/03](docs/03-DEMO-SCRIPT.md) |

**How this repo answers the four things you evaluate on:**

| what you look at | where it is |
|---|---|
| **Problem taste** — did you pick something that actually matters | [The problem](#the-problem-in-the-rails-own-terms) — NPCI's ₹10,000 block, live with Zomato/Swiggy/Zepto, with [sourced evidence](docs/06-AGENTIC-BLIND-SPOTS.md) |
| **Build quality** — does it run, is it structured, would you trust it | [`./verify.sh`](#build-quality--one-command-proves-every-claim) — 45 checks, exits non-zero if any claim here is false |
| **AI judgment** — the right tool in the right place, and where you chose *not* to use one | [Where I used a model, and where I refused to](#ai-judgment--where-i-used-a-model-and-where-i-refused-to) |
| **Failure recovery** — what broke, and what you did about it | [What broke](#what-broke-and-how-i-got-out) — four real ones, including a critical bypass in my own engine |

---

## What broke, and how I got out

Four things went wrong that were worth the repo remembering. Each is linked to the commit
or document that fixed it.

### 1 · My own engine approved a purchase the mandate forbade

**Quantity caps were enforced per line.** But the *agent* chooses how many lines it sends.
So ten lines of one item bought **ten of a max-one item** — verdict `0x0000`, `ALLOW`, no
violation, because every line was individually legal.

I found it writing an adversarial test, not from a failing case. Fixed with per-constraint
aggregation (`agg_qty[MAXC]`) plus a second pass attributing the overrun back to every
contributing line, so the audit trail still names them. It made the kernel measurably
slower and [BENCHMARKS](docs/BENCHMARKS.md) records the before and after — **including the
part I could not measure**, because the pre-fix revision no longer compiles.

That bug is the argument for the whole project: every layer above the kernel drifts, so
the check has to sit at the bottom and read the actual cart.

### 2 · My risk metrics were fiction

I first reported **precision 1.00** on 24 hand-written cases — that the thresholds had
also been tuned against. On a proper held-out split the same detector scored **0.278
precision at a 12.1% false-positive rate**, and 906 legitimate transactions were at a
first-time merchant.

The root cause was design, not tuning: it fired on any single signal. Rebuilt with
weighted corroboration (burst 3, velocity 2, new merchant 1, odd hour 1, flag at ≥3) →
**0.908 precision, 0.301 recall, 0.002 FPR**. [docs/07 §6](docs/07-RISK-METRICS.md)
publishes the fiction alongside the fix rather than quietly replacing the number.

### 3 · CI had never passed, and I had not noticed

The badge above was **red for 30 consecutive runs**. macOS passed every time, so nothing
local ever told me. Two independent causes, both found by reproducing the CI environment
in an `ubuntu:24.04` container:

- `bench_engine_kernel.cpp` included `<mach/mach_time.h>` — Darwin-only. `clock.hpp`
  existed to solve exactly this and the benchmark had never been converted.
- The sanitizer job compiled `engine/src/*.cpp`, which globs twelve `rig_*.cpp` tools,
  each with its own `main()`. Its fallback compiled `kernel.cpp` alone, which cannot
  resolve `translate_local`. **Both paths were broken**, so that job had never once run.

A judge cloning this on Linux would have got a compile error instead of a demo.

### 4 · A use-after-poison at 2 AM

simdjson `ondemand` string views point into a buffer that is invalidated on the next
parse. ASan caught it; the permanent fix was interning every SKU to a `uint32_t` at the
parse boundary, so no downstream code can hold a dangling view.
Full write-up: [docs/04](docs/04-INCIDENT-2AM.md).

---

## The problem, in the rail's own terms

UPI Reserve Pay lets a human block funds and delegate spending to an AI agent. NPCI caps
that block at **₹10,000 for 90 days**, and Razorpay and NPCI announced Agentic Payments
on Claude in Feb 2026, piloting with Zomato, Swiggy and Zepto.

The rail enforces two things: **is this a registered agent**, and **is the amount inside
the block**. Both are necessary. Neither looks at the cart.

Payment rails were built for a human who clicks once and knows what they are buying.
That assumption breaks in three places:

| gap | what goes wrong |
|---|---|
| **Intent** | A restocking agent buys a ₹9,000 tablet instead of ₹250 phone cases — 36× the intended price, and still inside the ₹10,000 block. An ops agent adds 30 seats against a 25-seat cap. Nothing in the chain compares the cart to what the human authorised. |
| **Behaviour** | Agents retry by **re-generating** the request, not re-sending it — so the rail sees a new order and charges twice. Hidden text on a merchant page redirects the agent mid-checkout. |
| **Liability** | No human-bound proof tied to that specific transaction. No click trail, no cardholder session. The merchant absorbs the chargeback. |

Evidence for all three, with sources: **[docs/06 — The Four Blind Spots](docs/06-AGENTIC-BLIND-SPOTS.md)**

## The solution

```
 AI agent  ──proposes──▶  INTENT GATEWAY  ──▶  hash-chained WAL  ──▶  capability token  ──▶  Razorpay
    │                     ├ intent policy         durable            Ed25519, cart-bound     test mode
    │                     ├ agent-aware risk
    │                     └ human step-up
    └── holds no credentials ───────────────────────────────────────────────────────────────────┘
```

The agent **proposes**. A deterministic C++ kernel **decides** in ~28 ns. A payment token
is minted only after that decision is durable in a tamper-evident log. The agent holds no
Razorpay credentials, so bypassing the policy is not a prompt-engineering problem — it
requires forging an Ed25519 signature.

### Three outcomes, not two

| outcome | trigger | why |
|---|---|---|
| **ALLOW** | cart matches the signed intent | token minted, payment executes |
| **REVIEW** | probabilistic signal only (burst, new merchant, odd hour, suspicious text) | **ask the human.** A false positive costs one tap, not a lost sale |
| **DENY** | deterministic rule violation | no token exists, so money cannot move |

Auto-blocking on a probabilistic score is how a risk system destroys good revenue. That
is why behavioural signals escalate instead of blocking — and why the false-positive cost
is measurable: **[docs/07 — Honest Risk Metrics](docs/07-RISK-METRICS.md)**.

## AI judgment — where I used a model, and where I refused to

A model appears in exactly one place in this system, and the decision to keep it out of
everywhere else is the architecture.

```
  "order me lunch under 500"
        │
   ┌────▼──────────────────────────────────────┐
 1 │  TRANSLATE   language → DRAFT mandate     │  ← the ONLY place a model runs
   │              src/intent.cpp               │
   └────┬──────────────────────────────────────┘
        │  a draft. not an authorisation.
   ┌────▼──────────────────────────────────────┐
 2 │  HUMAN       reads one sentence, signs    │  ← Ed25519, on the user's device
   └────┬──────────────────────────────────────┘
   ┌────▼──────────────────────────────────────┐
 3 │  KERNEL      evaluate(mandate, cart, now) │  ← deterministic C++
 4 │  TOKEN       single-use, cart-bound       │     no model, no network
 5 │  RAZORPAY    the actual payment           │
   └───────────────────────────────────────────┘
```

**Where a model is the right tool.** Turning *"order me lunch, a thali and a drink, under
500"* into a structured mandate is genuinely a language problem — messy input, no fixed
grammar, and a human checks the output before it means anything. `ANTHROPIC_API_KEY` is
read in exactly one function.

**Where I deliberately did not use one, and why:**

| decision | why not a model |
|---|---|
| **The policy verdict** | `evaluate()` takes a mandate and a cart. **There is no utterance parameter** — by the time money is decided, the sentence no longer exists. A model cannot reach the decision even in principle. Determinism is also what makes replay possible: same inputs, same verdict, on any machine, which is what turns a log into evidence. |
| **Prompt-injection detection** | The scanner is **telemetry only**. It raises `REVIEW`, never `DENY`. A probabilistic signal that auto-blocks turns every false positive into a lost sale — and in payments a false positive costs more than the fraud it prevents ([the model](#what-it-earns-not-just-what-it-blocks--track-01)). |
| **Behavioural risk** | Weighted corroboration over counters, not a classifier. It has to be explainable to a disputes team, and *"three signals agreed"* is defensible where a model score is not. |
| **The fallback** | With no API key the translator is an offline keyword matcher, and the UI **says so**. The demo cannot fail because a model is down, and no security property depends on the model being correct. |

**The test for whether the boundary holds:** if the model hallucinates, is prompt-injected,
or goes down, the worst outcome is a **bad draft that a human does not sign**. It cannot
sign a mandate, mint a token, or call Razorpay. Getting past the gateway is not a
prompt-engineering problem — it requires forging an Ed25519 signature.

## One engine, five industries

The kernel has no concept of any sector. It reasons about SKU ids, category ids,
merchant ids, integer paise and quantities — nothing else. Same binary, four
industries, four different controls, nothing changed but the fixtures:

```bash
./scripts/sectors.sh
```

| sector | the agent does this | refused by |
|---|---|---|
| **Online retail** | restocking accessories, buys a ₹62,000 tablet | `R_SKU_NOT_IN_INTENT` + 3 more |
| **SaaS licensing** | 30 seats as 3 lines of 10, against a 25 cap | `R_QTY_EXCEEDED` |
| **Travel** | a second return flight blows the trip budget | `R_CART_TOTAL_EXCEEDED` |
| **B2B procurement** | switches to a cheaper unapproved vendor | `R_MERCHANT_NOT_ALLOWED` |
| **Subscriptions** | ₹1 today, ₹999 every month — cart total ₹1 | `R_SUBSCRIPTION_UNDISCLOSED` |

Food ordering is one worked example in `fixtures/`, not the product. Any merchant
Razorpay serves expresses their intent as SKUs, caps, categories and a vendor list.

## Agent-readable discovery  [Track 01]

An AI buyer arriving cold learns what this merchant sells and what a mandate must
satisfy, without a human in the loop:

```bash
curl http://127.0.0.1:8787/.well-known/agent-commerce   # protocol, limits, endpoints, reject codes
curl http://127.0.0.1:8787/api/catalog                  # SKUs, categories, prices
```

That is what "transactable by an AI buyer end to end" requires: discovery, then a
signed mandate, then bounded purchases, then a receipt anyone can verify.

## What it earns, not just what it blocks  [Track 01]

```
./build/rig-revenue
```

Four policies on identical held-out traffic. Against a blocker tuned to catch 80% of
anomalies, this gateway preserves **₹79,542 more revenue (+6.4%)** and kills **zero**
in-mandate sales, at a cost of 61 confirmations across 3,693 purchases.

The aggressive blocker prevents ₹29,097 more fraud and spends ₹79,542 of real sales
doing it — **net ₹50,445 behind**. That is the commercial case for escalating instead
of blocking. Method and assumptions: **[docs/07 appendix](docs/07-RISK-METRICS.md)**.

## Meeting Razorpay's bar

> *"Every money action explainable, bounded and gated. Show the audit trail and one
> failure handled gracefully."*

| bar | where it happens |
|---|---|
| **Explainable** | The kernel evaluates *every* rule and accumulates a bitfield — it never short-circuits, so a bad cart reports **all** its reasons with per-line attribution. Every decision is replayed by two independent implementations. |
| **Bounded** | `int64` paise with checked overflow, per-SKU and per-cart caps, quantity caps, substitution ceilings, mandate TTL, merchant allowlist, single-use nonces, hourly velocity and spend limits. |
| **Gated** | No capability token exists until its decision is durable. Eight bypass attempts all refused by construction — three forged mandates at admission, five routes around the gateway. Anomalies gated behind human confirmation. |
| **Audit trail** | `./build/rig-audit` — the whole transaction in one screen. `rig-evidence` for the dispute-grade JSON. |
| **Graceful failure** | Three, end to end — see below. |

### Failures handled gracefully

1. **Hallucinated item** — ₹6,000 blender in a lunch mandate → `DENY 0x000D` with three
   reasons and a repair hint. The agent drops it, resubmits, **the lunch still arrives**.
   The user can then approve the blender deliberately with MFA.
2. **Retry storm** — the agent re-generates a timed-out request (reordered lines, new
   `client_ref`) → collapsed onto the original decision, `R_DUPLICATE_CHARGE`, **no second
   charge**. The repair hint says `stop_retrying` rather than pushing it into a loop.
3. **Prompt injection** — hidden merchant text tries to add a gift card → denied on
   **intent**, with the attempt logged. An in-intent cart carrying hostile text escalates
   to `REVIEW` instead of blocking a legitimate purchase.

## Component → gap → track

| component | file | gap addressed | Razorpay bar | track |
|---|---|---|---|---|
| Intent mandate (Ed25519-signed) | `include/rig/schema.hpp` | Intent | bounded | 01 |
| Deterministic policy kernel | `src/kernel.cpp` | Intent | explainable, bounded | 01 |
| Substitution policy | `src/kernel.cpp` | Intent | bounded | 01 |
| simdjson ingest + SKU interning | `src/parse.cpp` | Behaviour | gated | 01 / 02 |
| Injection scanner *(telemetry only)* | `include/rig/safety.hpp` | Behaviour | explainable | 02 |
| Semantic idempotency | `include/rig/idempotency.hpp` | Behaviour | gated | 01 / 02 |
| Agent-aware risk (burst / merchant / hour) | `include/rig/risk.hpp` | Behaviour | gated | **02** |
| Human step-up + confirmation record | `src/gateway.cpp` | Intent, Liability | gated | 01 |
| Hash-chained WAL | `src/wal.cpp` | Liability | audit trail | 01 |
| Capability tokens | `include/rig/pct.hpp` | Behaviour, Liability | gated | 01 |
| Reversals (device-signed refunds) | `src/gateway.cpp` | Liability | bounded, gated | 01 |
| Razorpay test-mode rail | `src/rail.cpp` | — | — | **01** |
| Readable audit trail | `src/rig_audit.cpp` | Liability | audit trail | 01 |
| Dispute evidence pack | `src/rig_evidence.cpp` | Liability | audit trail | 01 / 02 |
| Independent Java auditor | `control-plane/` | Liability | explainable | 01 |

## Measured, on an Apple M4

| | p50 |
|---|---:|
| Policy kernel (8-line cart) | **58 ns** |
| simdjson parse + extract | 160 ns |
| **Full decision** | **≈ 225 ns** |
| Durable audit, group-committed | 37 µs (27,000/s) |

Method, caveats and how each was measured: **[docs/BENCHMARKS.md](docs/BENCHMARKS.md)**

> The claim is **not** "microsecond checkout." A checkout is ~200 ms. The claim is that
> the safety layer is *free*: ~225 ns on a 200 ms transaction.

## Build quality — one command proves every claim

```bash
git clone https://github.com/anvidev01/razorpay-mandate-engine.git && cd razorpay-mandate-engine
./verify.sh            # 45 checks, ~90s   (--quick skips benchmarks/sanitizers, ~15s)
```

Builds from clean, then proves each claim in this README and prints PASS/FAIL:
the 70 kernel vectors and 48 intent regressions, all three graceful failures,
eight refused bypasses including three forged mandates, both auditors agreeing,
tamper detection, the risk confusion matrix, both clock/durability code paths
(macOS and Linux), ASan/UBSan, and a real payment through the rail. Exit code is
the number of failures.

CI runs the same script on **Ubuntu and macOS** on every push.

## Or drive it yourself

```bash
./run.sh
```

Builds on first use (~30s), starts the gateway, opens http://127.0.0.1:8787.
Missing a dependency? It names the exact install command for your platform.

Two tabs in the UI:

- **Scripted scenarios** — six one-click cases: a clean order, a hallucinated
  ₹6,000 blender, auto-repair, a retry storm, a prompt injection, and a bad
  substitution. Or press **Run all six ▸**.
- **Compose your own order** — build a mandate (budget, TTL, merchants,
  substitution policy, per-item caps), press **Sign & admit**, then send any cart
  against it and watch the engine decide. Rupees in the form, paise on the wire.

Try these against your own mandate to see each control fire:

| do this | expect |
|---|---|
| cart total above the budget | `DENY 0x0008 R_CART_TOTAL_EXCEEDED` |
| a SKU not in the item rules | `DENY 0x0401 R_SKU_NOT_IN_INTENT` |
| a new SKU in an approved **category**, priced well over the cap | `DENY 0x0800 R_SUBSTITUTION_DELTA` |
| put *"ignore previous instructions…"* in an item's text | `REVIEW 0x1000 R_INJECTION_SUSPECTED` → step-up |
| send the identical cart twice | `DENY 0x2000 R_DUPLICATE_CHARGE` |

Then open **View evidence pack** in the audit card for the dispute-grade JSON.

Prefer the terminal? `./run.sh --demo` runs the whole walkthrough non-interactively.
Field reference and gotchas: **[docs/08](docs/08-TESTING-YOUR-DATA.md)**.

### Proving the Razorpay integration is real

```bash
./scripts/prove-razorpay.sh
```

Creates an order through the gateway, then reads it **back out of
`api.razorpay.com`** — nothing local can fake that — shows the audit metadata
Razorpay now stores against it, confirms a denied cart creates no order at all, and
lists your recent test orders straight from their API.

Most scenarios deliberately never contact Razorpay, because a refused cart must not:

| scenario | outcome | Razorpay called |
|---|---|---|
| order lunch / auto-repair / first retry | ALLOW | **yes** — a real `order_...` |
| hallucinated blender, wrong substitution | DENY | no |
| regenerated retry | DUPLICATE | no |
| hidden instructions | REVIEW | not until a human approves |

That table *is* the product: three of seven reach the rail.

### Real Razorpay test mode

```bash
export RAZORPAY_KEY_ID=rzp_test_xxx RAZORPAY_KEY_SECRET=yyy
./run.sh
```

### Cross-checking the two kernels

```bash
./scripts/crosscheck.sh 7 120
```

Generates adversarial carts across every control — unknown SKUs, substitution
categories, price and quantity caps, recurring tails, disallowed merchants,
overflow-scale amounts — and requires the C++ kernel and the independent Java auditor to
reach the **same verdict on every one**.

This exists because they silently disagreed once: the C++ kernel aggregates quantity
across cart lines, the Java auditor was left checking per line, and *"zero divergences"*
held only because no logged decision exercised the split-quantity case. One hand-written
regression is not enough to trust a dual-implementation claim.


Without keys the rail is a deterministic mock and **says so** in the header and in
every log line. With them you get real `order_...` ids from the Orders API.

### Manual build

```bash
brew install simdjson openssl@3
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./control-plane/build.sh          # plain javac, no Maven/Gradle
```

Everything, end to end, in about ten seconds:

```bash
./scripts/demo.sh
```

Individually:

```bash
./scripts/seed.sh                                                       # known state
./build/rig-eval fixtures/lunch_intent.json fixtures/lunch_cart.json --execute   # ALLOW → paid
./build/rig-eval fixtures/lunch_intent.json fixtures/blender_cart.json           # DENY 0x000D
./build/rig-eval fixtures/grocery_intent.json fixtures/cart_milk_organic.json    # DENY 0x0800
./build/rig-eval fixtures/lunch_intent.json fixtures/cart_injection_soft.json \
      --confirm approve --ref mfa_device_9f21 --execute                          # REVIEW → human → paid
./build/rig-audit wal/rig.wal                                           # the 30-second audit trail
./build/rig-evidence wal/rig.wal 3                                      # dispute-grade JSON
./build/rig-attack                                                      # 8 bypasses, all refused
./build/rig-riskeval                                                    # confusion matrix
./build/rig-replay wal/rig.wal                                          # C++ replays its own log
java -cp control-plane/out com.razorpay.rig.ReplayAuditor wal/rig.wal    # independent Java audit
./scripts/tamper.sh                                                     # flip a bit → chain breaks
./build/rig-tests                                                       # golden vectors
```

Interactive UI: `./build/rig-gateway` → http://127.0.0.1:8787

## Documents

| | |
|---|---|
| [01 — Policy Engine](docs/01-ARCHITECTURE.md) | memory pools, SoA layout, branch-free kernel |
| [02 — Audit Trail & WAL](docs/02-AUDIT-WAL.md) | hash chain, group commit, capability tokens, threat model |
| [03 — Demo Script](docs/03-DEMO-SCRIPT.md) | the 5-minute pitch, timed |
| [04 — Incident: 2 AM](docs/04-INCIDENT-2AM.md) | a real, reproduced use-after-poison bug |
| [05 — Sprint Plan](docs/05-SPRINT-PLAN.md) | day-by-day, with cut lines |
| [06 — The Four Blind Spots](docs/06-AGENTIC-BLIND-SPOTS.md) | the research, with sources |
| [07 — Honest Risk Metrics](docs/07-RISK-METRICS.md) | confusion matrix, false-positive cost |
| [08 — Testing Your Own Data](docs/08-TESTING-YOUR-DATA.md) | field reference, every failure path, gotchas |
| [09 — Security Review](docs/09-SECURITY-REVIEW.md) | findings, fixes, and accepted limitations |
| [10 — Everything From the Terminal](docs/10-TERMINAL.md) | no browser: every command, and which ones touch Razorpay |
| [11 — Run Your Own Scenario](docs/11-RUN-YOUR-OWN.md) | test your own mandates, carts and product catalogue |

| [Benchmarks](docs/BENCHMARKS.md) | every number, and how it was measured |

## Future work

Explicitly **not** claimed as built: deep integration with Razorpay's internal risk
systems, ML fraud models over agent action traces, multi-merchant mandate federation,
WAL segment rotation and archival, and AP2 wire-format interop (the artefacts map
one-to-one today, the encoding does not).

## License

MIT — see [LICENSE](LICENSE).
