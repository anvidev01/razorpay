# 04 — Incident: "What broke at 2 AM, and how I got out"

**Status:** resolved · **Duration:** 02:14 – 04:51 IST · **Severity:** SEV-1 (data corruption in the audit log)
**Reproducible:** `engine/tests/repro_arena_uaf.cpp` — see §7. *This is not a story; it is a test in the repo.*

---

## 1. Symptom

Load test at 40 000 rps, sustained. At **02:14** the gateway took `SIGSEGV`. Three
properties made it nasty:

- It only happened **above ~41 000 rps**.
- It only happened after **~90 seconds** of sustained load.
- It **never happened under the debugger.** Classic Heisenbug — LLDB slowed the process
  below the threshold at which the bug is reachable.

Worse than a crash: some surviving runs wrote `POLICY_DECISION` records whose SKU field
was garbage. **The audit log — the one artefact whose integrity is the entire premise of
this project — was silently corrupt.** That is what made it a SEV-1 rather than a crash.

## 2. First look

```
* thread #1, stop reason = EXC_BAD_ACCESS (code=1, address=0x7f9a2c00e1)
  frame #0: libsystem_platform.dylib`_platform_memmove + 84
  frame #1: rig`wal::serialize_decision(...) at wal_writer.cpp:212
```

`wal_writer.cpp:212` is a `memcpy` of the SKU string into the WAL record. Inspecting
the source view:

```
(lldb) p line.sku
(std::string_view) $0 = (__data_ = 0x00000001200a4060, __size_ = 140244232048)
```

`__size_` is **140 terabytes**. Not a corrupt length — a `string_view` whose backing
memory had been reused, so both the pointer and the length were read out of whatever now
occupied that slab. A dangling view, not a bad calculation.

## 3. Why the sanitisers were silent — the actual lesson

The obvious move is ASan. I ran it. **It reported nothing, and exited 0.**

That is not ASan failing. It is ASan working exactly as designed, and it is *the*
trap of writing pool allocators:

> ASan tracks memory returned by `malloc`/`new`. My arena calls `malloc` **once**, at
> startup, for a 4 MB slab, and then sub-allocates by bumping a pointer. Every byte in
> that slab is `malloc`-live for the entire process lifetime. A use-after-free *inside*
> a pool is, to ASan, a perfectly ordinary read of live memory.

**A custom allocator makes your program invisible to your memory tools.** You get the
performance of a pool and you silently give up the entire sanitiser safety net — and
nothing warns you.

*(Aside: Valgrind was not an option — it has no arm64 macOS support at all, and `gdb`
is not practically usable on Apple Silicon either. On this platform the toolkit is
ASan/UBSan/TSan + LLDB. Valgrind's `VALGRIND_MEMPOOL_*` client requests are the exact
analogue of the fix below, if you are on Linux.)*

## 4. Fixing the tooling first

I stopped debugging the bug and instead taught the sanitiser about the arena — three
lines:

```cpp
Arena(size_t c) : base(malloc(c)), cap(c) { ASAN_POISON_MEMORY_REGION(base, cap); }

char* alloc(size_t n){ char* p = base + head; head += n;
                       ASAN_UNPOISON_MEMORY_REGION(p, n); return p; }

void  reset(){ ASAN_POISON_MEMORY_REGION(base, head); head = 0; }   // <-- the key line
```

Now arena memory is poisoned by default, unpoisoned only while genuinely handed out, and
**re-poisoned on reset**. Rebuild, rerun — **9 seconds** to a clean report:

```
==28469==ERROR: AddressSanitizer: use-after-poison on address 0x621000000120
READ of size 1 at 0x621000000120 thread T0
    #0 in main uaf.cpp:59
0x621000000120 is located 32 bytes inside of 4096-byte region [...]
SUMMARY: AddressSanitizer: use-after-poison uaf.cpp:59
```

Both builds are preserved in the repo as a regression test. `POISON=0` exits 0 and
prints a plausible checksum; `POISON=1` aborts with the report above. **Same bug. Same
binary logic. Only the instrumentation differs.**

## 5. Root cause

simdjson's On-Demand API returns `std::string_view`s that point **into the parser's
own buffer**, not into memory you own. My pipeline did:

```
parse cart ──> hold string_views of each SKU ──> push into audit ring ──> arena.reset()
                                                        │
                                        next request reuses those exact bytes
```

The audit ring retained borrowed views across the request boundary. Then:

- **Low load:** the ring entry was serialised and dropped before the slab was reused. Fine.
- **High load:** the next request arrived first, `reset()` rewound the arena, a
  *different-sized* cart re-used the slab, and the retained view now described
  whatever was there.

The **size dependency** is why 41 000 rps mattered: it wasn't the rate, it was that at
that rate a *large* cart (8 lines, arena offset 224) was reliably followed by a *small*
cart (1 line, offset 0..32) before the ring drained. The high offsets became stale
while still referenced. Under 41 k the ring always drained in time.

Two aggravating factors found in the same pass:
- The `padded_string` buffer was sized 1 KB. A >1 KB cart made simdjson **reallocate
  internally**, dangling every previously returned view *immediately* — a second,
  independent path to the same crash.
- `arena.head_` and the ring's `write_idx_` shared a cache line. `perf c2c`'s
  equivalent on macOS (Instruments' *System Trace*) showed the contention; it was not
  the root cause, but it widened the race window enough to make the bug reachable.

## 6. The fix — three layers, in priority order

**(1) Delete the bug class: own your bytes.** SKU strings are now interned to a
`uint32_t` **at the parse boundary**. Nothing downstream ever holds a `string_view` into
parser or arena memory. The kernel, the WAL, and the capability token see only integers.

> This is the real fix. The other two are defence in depth.

**(2) Make the mistake unrepresentable.** The audit ring now static-asserts its element
type:

```cpp
static_assert(std::is_trivially_copyable_v<T>);
static_assert(!contains_pointer_v<T>, "audit ring may not retain borrowed memory");
```

Anyone who tries to reintroduce a `string_view` into the ring gets a **compile error**,
not a 2 AM page.

**(3) Generation-tagged handles.** Arena references became `{index, generation}`;
`reset()` bumps the generation, so a stale handle dereferences to `nullptr` instead of
to someone else's data (§01.2.4).

Plus: `padded_string` sized to `MAX_CART_BYTES` and asserted, never grown; ring write
index padded to 128 B; ASan poisoning kept permanently in debug + CI builds.

## 7. Regression test

```bash
$ c++ -std=c++20 -O1 -g -fsanitize=address -DPOISON=0 engine/tests/repro_arena_uaf.cpp -o uaf0 && ./uaf0
serialized checksum 664 (POISON=0)          # silent — the trap

$ c++ -std=c++20 -O1 -g -fsanitize=address -DPOISON=1 engine/tests/repro_arena_uaf.cpp -o uaf1 && ./uaf1
==ERROR: AddressSanitizer: use-after-poison   # caught
```

CI runs the `POISON=1` build and **fails the pipeline if it does not abort.**
A test that asserts the sanitiser still catches the bug — because the thing that failed
here was not the code, it was the *ability to see* the code failing.

## 8. What it cost, and what it bought

| | Before | After |
|---|---:|---:|
| Kernel p50 (8-line cart) | 73 ns | **28 ns** |
| Kernel p99 | 210 ns | **36 ns** |
| Crash rate @ 45 k rps | ~1 per 90 s | 0 over 6 h |
| Corrupt WAL records | observed | 0 |

**The fix made it 2.6× faster.** Interning at the boundary removed every string compare
from the hot loop; the kernel went from comparing bytes to comparing `uint32_t`. The p99
improved 5.8× because string compares are data-dependent — long shared SKU prefixes were
producing exactly the tail latency the branch-free design was supposed to eliminate.

## 9. What I actually learned

1. **A custom allocator silently disables your memory tools.** If you write a pool, you
   owe it sanitiser instrumentation on the same day — otherwise you have traded a safety
   net for throughput without recording the trade.
2. **Fix the tooling before the bug.** Three lines of poisoning turned a two-hour
   Heisenbug into a nine-second report. I spent 90 minutes debugging blind before
   spending 5 minutes making the bug visible. That ordering was the mistake.
3. **Zero-copy is a lifetime contract, not a performance trick.** `string_view` means
   *somebody else owns this and may take it back.* At a request boundary, borrowing is
   a bug waiting for load.
4. **Prefer fixes that make the error unrepresentable.** The `static_assert` is worth
   more than the patch, because it survives me forgetting all of this.
