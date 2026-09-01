# Razorpay Intent Gateway

**A deterministic policy engine that makes an AI agent's payment provably match the
human's original intent — in 270 nanoseconds.**

Razorpay AI Buildathon · Track 01: AI Growth & Agentic Commerce

---

## The problem

UPI Reserve Pay lets an AI agent spend your money. The rail enforces exactly one thing:
**is the agent under its spending limit.** It does not look at the cart.

An agent told *"order me lunch under ₹500"* can put a **₹6,000 blender** in the cart and
the payment succeeds — because ₹6,000 is under the mandate limit. The rail checks
*budget*. Nobody checks *intent*.

## The approach

The Intent Gateway sits between the agent and the rail:

```
LLM agent ──proposes──> C++ policy engine ──> hash-chained WAL ──> capability token ──> Executor ──> Razorpay
   │                          270 ns              durable            Ed25519            holds keys
   └── holds no credentials ─────────────────────────────────────────────────────────────────┘
```

The agent **proposes**. A branch-free C++ kernel **decides**. A payment token is minted
only after the decision is durable in a tamper-evident log. The agent holds no Razorpay
credentials, so bypassing the policy isn't a prompt-engineering problem — it requires
forging an Ed25519 signature.

## Measured, on an Apple M4

| | p50 |
|---|---:|
| Policy kernel (8-line cart) | **28 ns** |
| simdjson parse + extract | 160 ns |
| **Full decision** | **≈ 270 ns** |
| Durable audit record (group-committed) | ~15 µs amortised |

Reproduce: `./engine/bench/run_all.sh` · Method and caveats: [docs/BENCHMARKS.md](docs/BENCHMARKS.md)

> The claim is **not** "microsecond checkout." The checkout is ~200 ms. The claim is
> that the safety layer is *free*: 270 ns on a 200 ms transaction.

## Hitting the bar

> *"Every money action explainable, bounded and gated. Show the audit trail and one
> failure handled gracefully."*

- **Explainable** — the kernel evaluates *every* rule and accumulates a verdict bitfield;
  it never short-circuits. A bad cart reports all its reasons. Every decision is replayable:
  `ReplayAuditor` re-executes the log and proves each verdict from its recorded inputs.
- **Bounded** — `int64` paise with checked overflow, per-SKU and per-cart caps, mandate TTL,
  merchant allowlist, single-use nonces.
- **Gated** — no capability token exists until its decision is durable. Four bypass
  attempts (`scripts/attack.sh`) are all refused by construction.
- **Graceful failure** — a denial returns repair hints, the agent auto-repairs, the lunch
  still arrives, and the user can step up with MFA to approve the blender deliberately.

## Documents

| | |
|---|---|
| [01 — Policy Engine Architecture](docs/01-ARCHITECTURE.md) | memory pools, SoA layout, the branch-free kernel |
| [02 — Audit Trail & WAL](docs/02-AUDIT-WAL.md) | hash chain, group commit, capability tokens, threat model |
| [03 — Demo Script](docs/03-DEMO-SCRIPT.md) | the 5-minute pitch, timed |
| [04 — Incident: 2 AM](docs/04-INCIDENT-2AM.md) | a real, reproduced use-after-poison bug |
| [05 — Sprint Plan](docs/05-SPRINT-PLAN.md) | day-by-day to submission, with cut lines |
| [Benchmarks](docs/BENCHMARKS.md) | every number, and how it was measured |

## Build

```bash
brew install simdjson openssl@3
./engine/bench/run_all.sh        # reproduce the latency budget
```
