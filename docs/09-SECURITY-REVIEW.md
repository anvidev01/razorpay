# 09 — Security Review

A full pass over the codebase looking for ways to move money the human did not
authorise. Everything below was found by **testing the running system**, not by reading
it. Each finding lists the exploit, the fix, and where the regression is pinned.

Reproduce: `./build/rig-tests` (43), `./build/rig-tests-intent` (25), `./build/rig-attack`.

---

## Critical

### V-B · Quantity caps were per-line, so `max_qty` was advisory

**The agent chooses how many lines to send.** A per-line check therefore caps nothing.

```
mandate : at most ONE lime soda, budget ₹1000
cart    : ten separate lines, each qty 1   (₹900 total)
before  : ALLOW 0x0000   ← human approved 1, agent got 10
after   : DENY  0x0002   R_QTY_EXCEEDED
```

This is exactly the class of failure the project exists to prevent, sitting in the core
kernel. Every other control was intact and this one silently wasn't.

**Fix** — quantities now accumulate per constraint across the whole cart, including
lines adopted by a substitution rule, and the overrun is attributed to every
contributing line. Pinned in `test_kernel.cpp` (split-across-lines, exact-cap,
substitute-aggregation).

---

## High

### V-H · The mandate was signed and verified by the same key

The gateway generated its own keypair, signed the mandate, then verified with that same
key. That proves the gateway agrees with itself — it says nothing about a human.

**Fix** — a separate `UserDevice` holds the signing key; the gateway is enrolled with
only the **public** half and can never sign a mandate. Admission verifies the signature
over the **exact bytes** the human approved, so nothing can be edited between the phone
and the engine. Three forgeries are now demonstrated in `rig-attack`:

```
--sign-with-attacker-device    REFUSED  unenrolled device (d444a00b…), expected 861f8264…
--raise-budget-after-signing   REFUSED  signature does not cover this mandate
--fabricate-a-signature        REFUSED  signature does not cover this mandate
enrolled device (control)      ADMITTED
```

The evidence pack now records `signed_by_device`, which is the field a chargeback
actually turns on.

### V-D · `Access-Control-Allow-Origin: *` on a money-moving API

Any page the user happened to be visiting could `POST` to `127.0.0.1:8787/api/decide`
and spend from a live mandate.

**Fix** — the header is gone. The UI is same-origin, so CORS bought nothing.
Added `X-Content-Type-Options: nosniff` and `Cache-Control: no-store`.

### V-C · Evidence packs were malformed for large carts

The emitter used a fixed `char[4096]`; a 10 KB cart truncated mid-string and produced
**unparseable JSON** — the dispute artifact failed precisely when the disputed order was
large.

**Fix** — the buffer is sized from the formatted length. Verified: a 10,373-byte cart
now round-trips intact.

---

## Medium

### V-E · Unbounded request bodies and no read timeout

`Content-Length` is attacker-controlled and the read loop appended without limit. A
large body grew memory without bound; a declared-but-unsent body held the
single-threaded accept loop open indefinitely.

**Fix** — 16 KB header cap, 256 KB body cap, `413` on breach, and a 5-second
`SO_RCVTIMEO`. Verified with a 400 KB POST and a stalled slowloris connection.

### V-I · An empty cart was ALLOWED and minted a token

`"lines": []` returned `ALLOW 0x0000` and minted a capability token bound to nothing,
burning a nonce for a zero-value order.

**Fix** — an empty cart is a schema violation. Pinned in `test_kernel.cpp`.

### V-G · Unbounded nonce set; silent step-up eviction

The burned-nonce set grew for the life of the process (slow leak). The 64-slot pending
table silently dropped step-ups when full, stranding an agent with a decision no human
could ever answer.

**Fix** — nonces are pruned once their token expires (a nonce cannot be replayed after
that). A full pending table evicts the oldest entry and logs it.

---

## Checked and found sound

| area | result |
|---|---|
| Path traversal on the static handler | `..`, `%2e%2e`, `//etc/...` → 400/404 |
| Secret leakage | keys go to curl options only; never logged, never in the WAL, never in a response |
| Malformed / adversarial cart JSON | empty, `{`, `null`, `[]`, missing fields, negative prices → all **fail closed** |
| Integer overflow on cart totals | `__builtin_mul/add_overflow` → `R_ARITH_OVERFLOW`, pinned |
| SKU interning collisions | admission rejects `INVALID`, so no constraint holds id 0 and an over-long cart SKU cannot match one |
| Merchant ids beyond the 64-bit mask | fails closed to `R_MERCHANT_NOT_ALLOWED` |
| WAL tamper | one flipped bit breaks the chain (`scripts/tamper.sh`) |
| Token replay / cart swap / expiry | refused, in `rig-attack` |
| Memory safety | ASan + UBSan clean across both suites |

---

## Accepted limitations (not defects — stated deliberately)

**The gateway trusts every local process.** It binds to `127.0.0.1` with no
authentication, so any process on the machine can call `/api/decide`. The demo's trust
boundary is the machine. Production needs the agent runtime to authenticate — mTLS or a
bearer token issued per agent session — and that is not built here.

**The HTTP server is single-threaded on purpose.** The `Gateway` holds mutable state
(idempotency table, agent baselines, pending step-ups) and is **not** thread-safe.
Serving connections concurrently would introduce data races in the policy engine, which
is far worse than a slow client occupying the loop for up to five seconds. Concurrency
belongs behind a proper server with one gateway instance per thread and a shared WAL —
out of scope here.

**The `UserDevice` keypair lives in the same process** for demo convenience. The trust
boundary is modelled correctly — the gateway holds only the public key and cannot sign —
but on real hardware that key belongs in the phone's secure element.

**The injection scanner is telemetry, not a control.** See `docs/06`.

**Behavioural risk bits are not replayable** from a single record. See `docs/07`.
