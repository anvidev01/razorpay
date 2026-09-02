# 07 — Agent-Aware Risk: Honest Metrics  [Track 02]

> Razorpay's Track 02 bar: **"Honest metrics including false-positive cost.
> Strictly defense-only."**

Reproduce everything here with `./build/rig-riskeval`.

---

## 1. The design rule that makes the metrics honest

**Behavioural signals produce `REVIEW`, never `DENY`.**

A hard intent violation (item not in the mandate, over the cap, expired) is deterministic
and blocks. A behavioural score is probabilistic, and auto-blocking on a probabilistic
signal converts every false positive into a declined payment and a lost sale.

Here a false positive costs **one confirmation prompt**. The purchase still completes.
That is the entire reason the engine has three outcomes instead of two:

```
HARD_DENY_MASK   -> DENY    deterministic, bounded rule violations
REVIEW_MASK      -> REVIEW  probabilistic signals: ask the human
                   ALLOW
```

`R_INJECTION_SUSPECTED` sits in `REVIEW_MASK` deliberately. Text-pattern detection is a
heuristic, so using it to block would manufacture false positives — and the structural
control (the item is not in the signed mandate) already denies real attacks on its own.

## 2. What is detected, and why these signals

Legacy velocity rules are tuned to human cadence and miss agents entirely.

| signal | why it is agent-specific |
|---|---|
| `R_VELOCITY_ANOMALY` | **burst**: >3 completions in 10 minutes. A human cannot buy four times in ten minutes; a looping or hijacked agent does exactly that. Also fires on >4/hour or >₹1,500/hour. |
| `R_NEW_MERCHANT` | agent transacting somewhere it has never used — the signature of a redirected agent |
| `R_ODD_HOUR` | outside observed active hours, with **±2h tolerance** so a 15:00 lunch is not suspicious |

Two properties keep the cold-start false-positive rate down:

- **Warm-up**: no opinion until the agent has 3 completed transactions. Firing on an
  agent's first-ever purchase is the classic cold-start false positive.
- **Only completed payments update the baseline.** A denied or reviewed cart never
  shapes the profile, so an attacker cannot train the baseline by spamming rejects.

Baselines are **rebuilt from the WAL on startup**, so an agent cannot reset its velocity
history by crashing between purchases.

## 3. Measured results

24 labelled transactions: 14 normal lunch purchases over a week, 8 genuinely anomalous,
2 legitimate-but-unusual (a late lunch, a second order the same day).

```
  confusion matrix
    true positives   5    false negatives  3
    false positives  0    true negatives  16

    precision 1.00   recall 0.62   false-positive rate 0.00
```

| metric | value | reading |
|---|---:|---|
| precision | **1.00** | nothing flagged was normal traffic |
| recall | **0.62** | 5 of 8 anomalies caught |
| false-positive rate | **0.00** | zero prompts on legitimate traffic |
| cost per false positive | **1 tap** | not a declined payment |

### The three misses are honest, and expected

All three false negatives are the **first three transactions of the burst**. The burst
rule fires on the *fourth* completion inside ten minutes, so the opening three are
allowed by design. A retry storm is caught on its fourth attempt, not its first.

That is a deliberate trade, not a bug: lowering the threshold to catch all five would
start flagging legitimate double-orders. **Recall 0.62 at precision 1.00 is the honest
number, and it is reported rather than tuned away.** Overfitting the thresholds to this
scenario set would produce a better-looking table and a worse product.

Note also that the duplicate-charge case — by far the most common agent failure — is
**not** handled here at all. It is caught deterministically by the idempotency layer
(`docs/06`), which has no false positives by construction because it compares cart
hashes rather than scoring behaviour. Risk scoring is the last line, not the first.

## 4. Strictly defense-only

- The module **blocks nothing on its own**. Its only output is *ask the human first*.
- No automated punitive action, no account action, no data sent anywhere.
- No profiling of the human. The baseline is per **agent session**, covering merchant
  ids, hour-of-day, count and amount. No PII, no device fingerprinting, no cross-user
  data.
- Signals are recorded in the audit log so a reviewer can see exactly why a step-up was
  requested.

## 5. Replayability caveat, stated plainly

Behavioural bits derive from **cross-transaction state**, so a single-record replay
cannot re-derive them. The auditors exclude `STATEFUL_RISK_MASK` from the reproducibility
check and report how many decisions carried such bits:

```
divergent : 0
note      : 2 decision(s) also carried behavioural risk bits, which derive from
            cross-transaction state and are recorded but not kernel-reproducible
```

The deterministic policy kernel remains 100% replayable, by two independent
implementations. The risk layer is recorded and explainable, but honestly labelled as
not reproducible from one record alone.

## Future work

- Per-merchant-category baselines rather than one profile per agent
- Sequence models over agent action traces (currently threshold rules only)
- Feeding confirmed step-up outcomes back as labels
