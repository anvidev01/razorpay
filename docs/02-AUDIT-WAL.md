# 02 — The Audit Trail: Making Bypass Structurally Impossible

> Razorpay's bar: *"Every money action explainable, bounded and gated.
> Show the audit trail and one failure handled gracefully."*
> This document is the answer to that sentence.

## 0. The threat we are actually defending against

The naive framing is "stop the LLM from buying the wrong thing." That is a *policy*
problem, and policy can be prompted around. The real requirement is stronger:

> **The LLM must not possess the capability to move money at all — not even if it is
> fully compromised, jailbroken, or replaced by an attacker.**

A guardrail the agent can be talked out of is not a guardrail. So the gateway is built
so that "ask the model nicely" is nowhere in the trust path.

---

## 1. Capability architecture — the agent holds no keys

```
   ┌──────────┐  proposes cart   ┌───────────────┐  verdict   ┌──────────────┐
   │  LLM     │ ───────────────> │  C++ POLICY   │ ─────────> │     WAL      │
   │  AGENT   │                  │    ENGINE     │            │ hash-chained │
   │          │ <─── denial ──── │  (270 ns)     │            └──────┬───────┘
   └──────────┘   + repair hints └───────┬───────┘                   │ durable
        │                                │ ALLOW only                │
        │  ✗ NO CREDENTIALS              ▼                           ▼
        │                        ┌───────────────┐          ┌────────────────┐
        └───── ✗ blocked ──────> │   EXECUTOR    │ <─────── │  PCT required  │
                                 │ holds RZP key │          │  + seq + hash  │
                                 └───────────────┘          └────────────────┘
```

Three structural facts:

1. **The agent has no Razorpay credentials.** They exist only inside the Executor
   process. There is no code path from the model to the payment API.
2. **The Executor refuses any request without a valid Payment Capability Token (PCT).**
   Not "logs a warning" — refuses.
3. **A PCT can only be minted by the policy engine, and only after the decision is
   durable in the WAL.**

### The PCT

```
PCT = Ed25519_sign(gateway_key, {
        decision_id, mandate_id, cart_hash, amount_paise,
        merchant_id, nonce, exp_ns, wal_seq, wal_record_hash })
```

The Executor validates the signature, checks `exp_ns` (60 s TTL), burns the `nonce`
(single-use, replay impossible), **and re-hashes the cart it is about to submit and
compares it to `cart_hash`.** That last check is what stops a swap *after* approval:
an approved lunch cart cannot be exchanged for a blender cart between decision and
execution, because the hash would not match.

> **Bypass requires forging an Ed25519 signature.** It is not a policy control that can
> be prompted around; it is a cryptographic one. That is the difference between a demo
> and a payments product.

---

## 2. The Write-Ahead Log

### 2.1 Record format

```
 offset  size  field
   0      4    len            (u32, total record bytes)
   4      4    crc32c         (over bytes 8..len)
   8      8    seq            (u64, monotonic, gap-free)
  16      8    wall_ns        (u64, for humans)
  24      8    mono_ns        (u64, for deltas — immune to clock skew)
  32      1    type           (u8, see 2.3)
  33      1    version        (u8)
  34      2    flags          (u16)
  36      N    payload        (canonical CBOR)
  36+N   32    prev_hash      (BLAKE3 of previous record)
  68+N   32    this_hash      (BLAKE3(prev_hash || bytes[0 .. 36+N]))
```

**The hash chain is the tamper-evidence.** Altering any historical record invalidates
every subsequent `this_hash`. The chain head is signed every 1 000 records
(`ANCHOR`), so an auditor holding only the gateway's public key can verify the entire
log from a handful of anchors — including detecting *truncation*, which a CRC alone
cannot.

### 2.2 The ordering invariant — this is the enforcement

> **A PCT does not exist until its `POLICY_DECISION` record is durable.**

```
1. append CART_PROPOSED   ─┐
2. append POLICY_DECISION ─┴─ same group-commit batch
3. F_FULLSYNC batch                 ◄── the fence
4. only now: mint PCT, embedding wal_seq + record hash
5. Executor validates PCT; may re-read WAL[seq] to confirm
```

Steps 3 and 4 cannot be reordered — the minter is not given the decision until the
commit thread signals the batch durable. Therefore:

**A payment that is not in the audit log is cryptographically unconstructible.**

That sentence is the submission's core claim, and it is a structural property, not a
promise.

### 2.3 Record types — the complete money lifecycle

| Type | Captures | Why a judge cares |
|---|---|---|
| `MANDATE_ISSUED` | NL utterance, LLM-extracted schema, **human confirmation**, Ed25519 sig | provenance of authority |
| `CART_PROPOSED` | agent id, canonical cart, `cart_hash`, model id, prompt hash | provenance of the *proposal* |
| `POLICY_DECISION` | engine version, schema hash, **verdict bitfield, every rule that fired**, eval ns | **explainability** |
| `CAPABILITY_ISSUED` / `_DENIED` | PCT id, nonce, TTL | gating |
| `PAYMENT_ATTEMPTED` / `_RESULT` | Razorpay order/payment id, HTTP status | the money action itself |
| `REMEDIATION` | repair hint, user step-up, outcome | **graceful failure** |
| `ANCHOR` | signed chain head | tamper-evidence |

Recording the **model id and prompt hash** on `CART_PROPOSED` means that after an
incident you can answer "which model version started proposing blenders, and when?" —
the question an actual payments post-mortem asks.

### 2.4 Group commit — the measured problem, and the fix

Measured on the target machine:

| | p50 |
|---|---:|
| `write()` only | 2.2 µs |
| `fsync()` | 33.5 µs |
| **`fcntl(F_FULLFSYNC)`** | **3 960 µs** |

> **On macOS, `fsync()` does not flush the drive's write cache.** Only
> `fcntl(fd, F_FULLFSYNC)` does. A WAL built on `fsync()` on this platform is *not*
> durable across power loss and the audit guarantee is a fiction. This is the single
> most important platform detail in the project, and it is the kind of thing that
> separates a working demo from a payments engineer's demo.

4 ms per transaction is unacceptable; lying about durability is worse. So: **group
commit.**

```
decisions ──> per-thread SPSC ring ──> commit thread ──> one F_FULLFSYNC per batch
                                        (batch = 256 records OR 2 ms, whichever first)
```

| Batch | Amortised durable cost / decision | Ceiling |
|---:|---:|---:|
| 1 | 3 960 µs | 252 /s |
| 64 | 62 µs | 16 100 /s |
| **256** | **15.5 µs** | **64 600 /s** |

The decision is returned to the agent at **270 ns**; the PCT is released when the batch
commits. Latency is honestly reported as two numbers, never conflated:
**decision 270 ns / durable-and-payable ~15 µs amortised (2 ms worst case).**

---

## 3. The Java control plane

The split is deliberate: **C++ owns the hot path and the WAL writer; Java owns
everything that must be trustworthy but not fast.**

```
control-plane/  (Java 21)
  MandateService     — NL -> schema extraction, human confirm, Ed25519 signing
  WalReader          — MappedByteBuffer over the WAL, read-only
  ChainVerifier      — BLAKE3 chain + ANCHOR signature verification
  ReplayAuditor      — re-executes recorded decisions, asserts verdicts match
  Executor           — the ONLY holder of Razorpay API keys; enforces PCT
  AuditApi           — REST surface for the demo UI
```

Java reads the WAL through a plain read-only `MappedByteBuffer` — **no JNI, no Panama,
no `--enable-preview` required** (FFM is still preview on JDK 21; adopting it here would
be schedule risk for zero benefit). The auditor invokes the C++ replay binary as a
subprocess. If time permits, the FFM upgrade is a stretch goal, not a dependency.

### 3.1 The replay auditor — the strongest claim in the submission

Because `evaluate()` is a pure function of its recorded inputs (§01.4c), the auditor can
re-execute every historical decision offline and assert the verdict matches what was
recorded:

```
$ java -cp out com.razorpay.rig.ReplayAuditor wal/
  chain    : 128,441 records, BLAKE3 chain INTACT, 128 anchors verified
  replay   : 128,441 decisions re-executed
  divergent: 0
  ✅ every money action in this log is reproducible from its inputs
```

This turns "explainable" from a claim into a **verifiable property**. Not "here are
some logs" — "here is a proof that the engine did what the policy says, for every
transaction, and you can run it yourself."

---

## 4. Threat model

| Attack | Defence | Structural? |
|---|---|---|
| LLM proposes out-of-intent cart | kernel DENY, all reasons logged | ✅ |
| LLM calls Razorpay directly | holds no credentials | ✅ |
| LLM forges a PCT | requires Ed25519 private key | ✅ |
| Cart swapped after approval | `cart_hash` re-checked by Executor | ✅ |
| PCT replayed | single-use nonce, burned in WAL | ✅ |
| Audit record deleted / edited | BLAKE3 chain + signed anchors | ✅ (detect) |
| Log truncated | signed anchors bound the head | ✅ (detect) |
| Prompt injection in item names | strings interned to `uint32_t` at the boundary; kernel never sees text | ✅ |
| Engine crashes | Executor requires PCT; no engine → no PCT → **no payment** | ✅ |
| Mandate expired | `not_after_ns`, checked every cart | ✅ |
| In-memory schema tampering | SipHash integrity tag re-checked per cart | ✅ |

**Every row is enforced by construction, not by asking the model to behave.**
