# Razorpay Intent Gateway

**An AI payment security layer that sits between an AI buyer and the payment rail, so
every money action an agent takes is explainable, bounded and gated — and provable
afterwards.**

Razorpay AI Buildathon · **Track 01 — AI Growth & Agentic Commerce** (primary) ·
**Track 02 — AI Risk Manager** (secondary)

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
| **Intent** | The agent buys a ₹6,000 blender instead of a ₹400 lunch, or swaps ₹60 milk for ₹180 organic. ₹6,000 < ₹10,000, so every system approves it. |
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

The agent **proposes**. A deterministic C++ kernel **decides** in ~31 ns. A payment token
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

## Meeting Razorpay's bar

> *"Every money action explainable, bounded and gated. Show the audit trail and one
> failure handled gracefully."*

| bar | where it happens |
|---|---|
| **Explainable** | The kernel evaluates *every* rule and accumulates a bitfield — it never short-circuits, so a bad cart reports **all** its reasons with per-line attribution. Every decision is replayed by two independent implementations. |
| **Bounded** | `int64` paise with checked overflow, per-SKU and per-cart caps, quantity caps, substitution ceilings, mandate TTL, merchant allowlist, single-use nonces, hourly velocity and spend limits. |
| **Gated** | No capability token exists until its decision is durable. Five bypass attempts all refused by construction. Anomalies gated behind human confirmation. |
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
| Razorpay test-mode rail | `src/rail.cpp` | — | — | **01** |
| Readable audit trail | `src/rig_audit.cpp` | Liability | audit trail | 01 |
| Dispute evidence pack | `src/rig_evidence.cpp` | Liability | audit trail | 01 / 02 |
| Independent Java auditor | `control-plane/` | Liability | explainable | 01 |

## Measured, on an Apple M4

| | p50 |
|---|---:|
| Policy kernel (8-line cart) | **30.8 ns** |
| simdjson parse + extract | 160 ns |
| **Full decision** | **≈ 225 ns** |
| Durable audit, group-committed | 37 µs (27,000/s) |

Method, caveats and how each was measured: **[docs/BENCHMARKS.md](docs/BENCHMARKS.md)**

> The claim is **not** "microsecond checkout." A checkout is ~200 ms. The claim is that
> the safety layer is *free*: ~225 ns on a 200 ms transaction.

## Build and run

```bash
brew install simdjson openssl@3
cmake -S engine -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./control-plane/build.sh          # plain javac, no Maven/Gradle
```

Everything, end to end, in about ten seconds:

```bash
./scripts/demo.sh
```

Razorpay test mode — without keys the rail is a deterministic mock and **says so** in
every log line:

```bash
export RAZORPAY_KEY_ID=rzp_test_xxx RAZORPAY_KEY_SECRET=yyy
./build/rig-eval fixtures/lunch_intent.json fixtures/lunch_cart.json --execute
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
./build/rig-attack                                                      # 5 bypasses, all refused
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
| [Benchmarks](docs/BENCHMARKS.md) | every number, and how it was measured |

## Future work

Explicitly **not** claimed as built: deep integration with Razorpay's internal risk
systems, ML fraud models over agent action traces, multi-merchant mandate federation,
WAL segment rotation and archival, and AP2 wire-format interop (the artefacts map
one-to-one today, the encoding does not).
