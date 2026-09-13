# Implementation log — `find_cover.h` optimization port

Tracks each item from `improvement_plan.md` as it's actually implemented,
per that plan's "Per-item workflow". One entry per item, in order.

Regression harness: `validate_find_cover.cpp` (repo root) — calls
`find_cover::find_all_covers_parallel<P,K>()` directly for each fixed
`(K,P)` test case. It never includes `lift.h`/`lift_strategy.h`, so Stage 2
(lifting) is never invoked — every entry below verifies Stage 1 only, per
the plan's regression set:

| K | Primes |
|---|---|
| 10 | 127, 199, 461 |
| 11 | 131, 199 |
| 12 | 139, 199, 211 |

**Choosing the larger primes (461 for K=10, 211 for K=12):** the goal was
one case per K landing near ~2 minutes on the unoptimized baseline, found
by probing candidates from `LrcVerifier<K>`'s prime list (`main.cpp`):

| K | P (probed) | count | time |
|---|---|---|---|
| 10 | 271 | 11 | 10.08s |
| 10 | 331 | 14 | 40.67s |
| 10 | 401 | 2 | 75.59s |
| 10 | **461 (chosen)** | 1 | 118.89s |
| 12 | **211 (chosen)** | 426537 | 102.35s |

Runtime doesn't scale smoothly with `P` for K=10 in this range (solution
`count` collapses from thousands at P=199 down to single digits by
P≈271+, so timing is dominated by exhaustive near-empty search rather than
enumeration volume) — hence probing several candidates rather than
extrapolating from a single ratio.

Correctness check: for each case, sort all solutions into a canonical
order and take `sha256` of the resulting text dump (one solution per line,
space-separated speeds). Two runs are "byte-identical" iff every case's
`(count, sha256)` pair matches. Full solution dumps aren't stored in git
(the raw baseline dump is 38MB uncompressed) — the hash is the durable
record; a mismatch is re-diagnosed by re-running both binaries and diffing
their raw output directly.

---

## Baseline (pre-optimization, current `find_cover.h`)

**Commit:** `68e8813` (current `HEAD` at the time of this run — no
optimization items from `improvement_plan.md` applied yet).
**Build:** `clang++ -std=c++23 -march=native -O3 -I. validate_find_cover.cpp -o validate_find_cover`
**Machine:** local (`run.sh`'s `clang++ -march=native`), all 8 cases run
sequentially in one process, wall time well under the 180s/case budget.

| K | P | count | time | sha256 |
|---|---|---|---|---|
| 10 | 127 | 8228 | 0.108624s | `0d3d183b13d4d7eb268960a2651b1ba45a1c1e0e95c5b9ccebf8a3d67e0b1bc5` |
| 10 | 199 | 4417 | 2.881222s | `d63a19686a5c77140c02c91d1c4561c277b4a39a60089b16e95f426136e8b07d` |
| 10 | 461 | 1 | 116.930165s | `65e30693f1c92b49f82f3d909444be02911cbd365931405c06eed475cfc5023d` |
| 11 | 131 | 40615 | 0.541452s | `d4f09adfe20bb00db597c76626ab3f456d932b76e73fedb5b63f09c441622384` |
| 11 | 199 | 18516 | 11.089837s | `9f665ec2919085a926bf36bfa9bfb949065a311d8c93228ea150c6c23636bd31` |
| 12 | 139 | 641960 | 6.273177s | `c3933c974792b60e23f2f31e133fbd3a39b7e0d04652a9b4fe4e271b9c33e964` |
| 12 | 199 | 494183 | 68.219173s | `745c2f2e17af101dfbaf260109b5f289d9a9fb244129bba318e2772116195093` |
| 12 | 211 | 426537 | 115.294216s | `d2e57650480177e62ab3a390a31d053e89d0511905dad9c7377f9769026ffa1b` |

All 8 cases completed well inside the 180s/case timeout (largest single
case, K=10/P=461, took ~117s — as intended, close to the ~2 minute
target). Total wall time for all 8 cases in one process: ~321s (~5.4 min).
The original 6 cases reproduced identical `(count, sha256)` pairs to the
first baseline run (P=127/199 timings shifted by run-to-run noise of a
few ms, as expected), confirming the harness is deterministic.

These `(count, sha256)` pairs are the ground truth every subsequent item
in `improvement_plan.md` must reproduce exactly. The `count` column alone
already gives a cheap sanity check; the hash catches any change that
alters *which* solutions are found while accidentally preserving the
total count.

**Superseded note:** the hashes above come from `validate_find_cover.cpp`,
which has since been deleted and replaced by `test.cpp` (a different,
permanent, self-contained oracle with its own canonical dump format —
its hashes are *not* the same strings as the ones above, only the same
`(K,P)` cases and `count`s). Every entry from here on validates against
`test.cpp`'s fixed table (see `improvement_plan.md`'s per-item workflow
section for those values).

---

## Item 1: attempted, then discarded

Item 1 (irredundant/redundant search decomposition) was implemented,
revised once to fix a real overlap bug and a 10-22x wall-clock regression,
and verified correct on 7/8 oracle cases — see this log's git history
(commit `b652bb9` on branch `Allikvere`) for the full record. It was then
discarded on this branch (`Allikvere-no-decompose`, reset to `9c14ac6`) by
request, so `src/find_cover.h` here has none of that code: no `DfsIrred`,
no `Target` template parameter, no `find_all_irredundant_covers`/
`find_all_redundant_covers`. Items from here on build on the plain
`Dfs`/`find_all_covers_parallel` structure only.

---

## Item 2: attempted, then reverted — the bound never fires here

Per the plan: only basegen's `avail.count() < slots` half ports here (not
v6.4's `need >= slots`, which relies on irredundancy this general search
doesn't have).

**Change attempted (`src/find_cover.h`):**
- `AvailableChoice` gained an incrementally-maintained `_availableCount`
  counter (starts at `P/2`, decremented in `eliminate()`) and a public
  `availableCount()` accessor.
- `early_return_bound()` gained an unconditional check at the very top,
  ahead of the existing `K - 4` gate:
  `if (state.choice.availableCount() < K - (int)state.elems.size()) return true;`

**Correctness held** (built `test.cpp`, unmodified permanent oracle,
against this change — all 8 fixed cases byte-identical). A first timing
pass showed 0-6.6% improvement, which looked plausible but turned out to
be measurement noise, not this check's effect — the first implementation
attempt had a bug: `_availableCount` was declared and read, but never
actually decremented in `eliminate()`, so it silently stayed at `P/2`
forever. Fixed that, then, before re-measuring, checked the actually
important question first: does the bound ever fire at all?

**Instrumented directly** (`g_item2_calls`/`g_item2_hits` atomic counters
around the check, in a scratch copy of the header, not committed) and
ran 5 cases:

| K | P | node calls | hits | hit rate |
|---|---|---|---|---|
| 10 | 127 | 3,155,705 | 0 | 0% |
| 10 | 199 | 53,928,307 | 0 | 0% |
| 11 | 131 | 9,675,460 | 0 | 0% |
| 11 | 199 | 167,992,842 | 0 | 0% |
| 12 | 139 | 66,947,581 | 0 | 0% |

**Zero hits across ~300M node evaluations.** The bound is correct but
vacuous for this codebase's problem sizes: `eliminate()` only fires on
candidates that pass the `cover(i)[nextToCover]` filter (classes covering
the *rarest* uncovered point), and `get_next_to_cover`'s whole purpose is
to pick the point with the fewest covering classes — so the number of
eliminations per level stays small everywhere, `state.choice` gets
restored after each level's loop, and cumulative eliminations along any
root-to-node path never approach closing the `P/2` (63-230 for these
primes) vs. `K` (10-12) gap. `P/2 >> K` here, and the search structure
itself (MRV branching) keeps it that way at every node, not just at the
root.

**Decision: reverted.** Correct-but-inert code that costs a small,
constant per-node overhead for zero pruning benefit isn't worth carrying.
`src/find_cover.h` is back to its pre-item-2 state (`git checkout --`
against this branch's own history — no new commit for the attempt).

---

## Item 3: `Bitset<N>` type + bitset-backed class availability

**Change:**
- Added `Bitset<N>` in its own header, `src/bitset.h`: fixed-word bitset
  (`uint64_t w[NW]`) with `set`/`reset`/`test`/`count`/`any`, a `BitRef`
  proxy so `operator[]` supports both read and write (matching
  `std::bitset`'s two forms), and free `|`/`&`/`~` (`~` masks the last
  word's padding bits via `TAIL_MASK` so they can't leak into a later
  `count()`/`any()`).
- `Context<P,K>::CoveredBitset` (`src/find_cover.h`) changed from
  `std::bitset<P/2>` to `Bitset<P/2>` — drop-in, same call sites unchanged.
- `AvailableChoice`'s `ElimArray` changed from `std::array<char, P/2>` to
  `Bitset<P/2>` — same "eliminated" semantics as before (bit set = class
  eliminated, matching the old array's `1`), just `_eliminated.test(i)`/
  `_eliminated.set(i)` instead of array indexing. Kept `_remaining` as-is
  per the plan (not a basegen idea, but find_cover.h's own optimization
  basegen doesn't have).
- Removed the now-unused `<bitset>` include from `find_cover.h`.

**Verification:** `test.cpp` (unmodified), all 8 fixed cases, run twice
(once mid-development, once again on the final on-disk state with no
further code changes) — both runs byte-identical to the oracle.

| K | P | count | sha256 match |
|---|---|---|---|
| 10 | 127 | 8228 | MATCH |
| 10 | 199 | 4417 | MATCH |
| 10 | 461 | 1 | MATCH |
| 11 | 131 | 40615 | MATCH |
| 11 | 199 | 18516 | MATCH |
| 12 | 139 | 641960 | MATCH |
| 12 | 199 | 494183 | MATCH |
| 12 | 211 | 426537 | MATCH |

**Timing: inconclusive, machine contention.** A first timing pass (on an
earlier, since-superseded version of this same change) showed a real but
small ~13% slowdown on the largest case and flat/noise elsewhere,
consistent with this item being a pure enabler per the plan ("nothing
below is cheap without it") rather than a standalone win. A second timing
pass, on the final on-disk state, instead showed 16-27x slowdowns on the
three longest-running cases (P=461/K=10, P=199/K=12, P=211/K=12) while
the five short cases stayed near baseline — but `uptime` at the time
showed load averages of 12/34/30 on a ~10-core M4, i.e. the machine was
running far more concurrent work than it has cores for (residue from the
many background builds/searches stacked up over this session). That
selective pattern — only the longest-running cases hit hard, short ones
normal — is the signature of contention exposure time, not a real
per-node cost from a char-array-to-bitset rename. Not treating either
timing pass as reliable; a clean, isolated re-measurement is needed before
drawing a timing conclusion for this item. Correctness is solid regardless.

---

## Item 4: transposed per-point candidate table `cand[point]`

**Change (`src/find_cover.h`):** `Context` gained a second
`std::array<CoveredBitset, P/2> mCand` alongside the existing `mCover`,
filled in the same constructor loop (now an `if` instead of an
unconditional bool assignment, since a class/point pair needs setting a
bit in *both* tables when the cover condition holds), plus a `cand(pos)`
accessor mirroring `cover(i)`. Purely additive — nothing in `Dfs`/
`AvailableChoice`/`find_all_covers_parallel` reads `cand()` yet (that's
item 6); this item only stores the transpose.

**Verification:** `test.cpp`, all 8 fixed cases, byte-identical.

| K | P | count | sha256 match |
|---|---|---|---|
| 10 | 127 | 8228 | MATCH |
| 10 | 199 | 4417 | MATCH |
| 10 | 461 | 1 | MATCH |
| 11 | 131 | 40615 | MATCH |
| 11 | 199 | 18516 | MATCH |
| 12 | 139 | 641960 | MATCH |
| 12 | 199 | 494183 | MATCH |
| 12 | 211 | 426537 | MATCH |

**Timing:** not measured for this item. `mCand` is filled once per
`(P,K)` instantiation, inside `Context`'s constructor (via the global
`context<P,K>` singleton) — negligible relative to the whole search — and
nothing in the hot path reads it yet, so there is nothing for a timing
pass to meaningfully show before item 6 lands. (Also: the machine's load
average was still elevated during this run from prior session activity,
so a timing comparison right now would be no more trustworthy than item
3's second pass was.)

---

## Item 5: word-scan bit iteration

**Change, revised after review:** the first pass (below, for the record)
added a free `for_each_set_bit` function and a hand-written early-exit
scan in `get_next_to_cover`. Reworked at the reviewer's request into:
- `Bitset<N>::for_each(auto&& f) const`: a `const`-qualified *member*
  (visits only the set bits, word + `__builtin_ctzll`, cost proportional
  to popcount instead of `N`) instead of a free function.
- `Bitset<N>`'s internals (`w[]`, `NW`, `TAIL_REM`, `TAIL_MASK`, `BitRef`)
  made `private`; `operator|`, `operator&`, `operator~` converted from
  free functions (which had reached into `a.w[]` directly) to `const`
  members, so they no longer need `friend` access.
- `AvailableChoice`'s constructor and `eliminate()`: `for (pos = 0; pos <
  bitlen; ++pos) if (context.cover(i)[pos]) ...` replaced with
  `context<P,K>.cover(i).for_each([&](int pos) { ... });`.
- `AvailableChoice::get_next_to_cover`: `(~current_covered).for_each(...)`
  instead of testing every index — **without** the early-exit break the
  first pass had (basegen's `rarest_uncovered` returning as soon as
  `_remaining <= 1`), since the member `for_each` has no way to signal
  "stop" back to the caller. Verified this doesn't change *which*
  position is returned: `for_each` visits words `0..NW-1` in order and,
  within each word, repeatedly takes the lowest set bit — i.e. strictly
  ascending position order, identical to the original `for` loop's order,
  so the `_remaining[pos] < best` tie-break (keeps the first-seen position
  on a tie) picks the same position either way. Only effort spent differs
  (no early stop), not the result.

**Verification:** `test.cpp`, all 8 fixed cases, byte-identical — run
twice (once on the first free-function pass, once on this reworked
member-based version).

| K | P | count | sha256 match |
|---|---|---|---|
| 10 | 127 | 8228 | MATCH |
| 10 | 199 | 4417 | MATCH |
| 10 | 461 | 1 | MATCH |
| 11 | 131 | 40615 | MATCH |
| 11 | 199 | 18516 | MATCH |
| 12 | 139 | 641960 | MATCH |
| 12 | 199 | 494183 | MATCH |
| 12 | 211 | 426537 | MATCH |

**Timing vs. baseline**, this reworked version (machine load average
6.59-13.55 at the time, clean enough after the earlier contention issue
settled):

| K | P | baseline | item 5 | Δ |
|---|---|---|---|---|
| 10 | 127 | 0.109s | 0.112s | +2.8% (noise) |
| 10 | 199 | 2.881s | 1.800s | -37.5% |
| 10 | 461 | 116.930s | 63.912s | -45.3% |
| 11 | 131 | 0.541s | 0.406s | -24.9% |
| 11 | 199 | 11.090s | 6.363s | -42.6% |
| 12 | 139 | 6.273s | 4.533s | -27.7% |
| 12 | 199 | 68.219s | 41.735s | -38.8% |
| 12 | 211 | 115.294s | 78.920s | -31.6% |

A real, consistent 25-45% speedup on every non-trivial case — as good as
or slightly better than the first pass's 23-43%, despite dropping the
early-exit break, matching the plan's "Medium-Large" impact rating.

<details>
<summary>First pass (superseded by the rework above; kept for the record)</summary>

Added a free `for_each_set_bit(const Bitset<N>&, F&&)` and a hand-written
early-exit scan in `get_next_to_cover` (return as soon as a position with
`_remaining <= 1` is found, matching basegen's `rarest_uncovered`).
Timing vs. baseline at the time (load average 13-17, elevated):

| K | P | baseline | item 5 (first pass) | Δ |
|---|---|---|---|---|
| 10 | 127 | 0.109s | 0.116s | +6.4% |
| 10 | 199 | 2.881s | 1.888s | -34.5% |
| 10 | 461 | 116.930s | 67.257s | -42.5% |
| 11 | 131 | 0.541s | 0.418s | -22.8% |
| 11 | 199 | 11.090s | 6.691s | -39.7% |
| 12 | 139 | 6.273s | 4.780s | -23.8% |
| 12 | 199 | 68.219s | 45.960s | -32.6% |
| 12 | 211 | 115.294s | 86.723s | -24.8% |

</details>

---

## Item 6: restrict candidate enumeration to `cand(t) & avail`

**Change (`src/find_cover.h`):**
- `AvailableChoice` gained an `available()` accessor (`ElimArray
  available() const { return ~_eliminated; }`) so callers can get the
  full "still available" bitset, not just per-index `isEliminated`
  queries.
- `Dfs::run`'s main loop: replaced `for (i = 0; i < P/2; ++i) if
  (!isEliminated(i) && (nextToCover==-1 || cover(i)[nextToCover])) ...`
  with `(nextToCover == -1) ? avail : (context<P,K>.cand(nextToCover) &
  avail)`, then `choices.for_each(...)` — only classes that are both
  available *and* cover the target point, word-scanned directly, instead
  of testing all `P/2` indices.
- `early_return_bound`'s `bestCovering`/`bestCovering_next`: `bestCovering`
  still scans all of `available()` (a bound over every remaining class),
  now via word-scan instead of testing all `P/2` and filtering by
  `isEliminated`; `bestCovering_next` restricted to `cand(nextToCover) &
  available()` instead of scanning all `P/2` and filtering by
  `cover(i)[nextToCover]`.

**Found and fixed during review:** the reviewer renamed `AvailableChoice`'s
accessor from `avail()` to `available()`, but `Dfs::run` still called
`state.choice.avail()` — a one-line fix (renamed the call site to match).
Confirmed by rebuilding: it doesn't compile without the fix, and does
(byte-identical to the oracle) with it.

**Verification:** `test.cpp`, all 8 fixed cases, byte-identical — checked
twice (once before the `avail`/`available` rename, once after, both
against the same on-disk state you'd get right now).

| K | P | count | sha256 match |
|---|---|---|---|
| 10 | 127 | 8228 | MATCH |
| 10 | 199 | 4417 | MATCH |
| 10 | 461 | 1 | MATCH |
| 11 | 131 | 40615 | MATCH |
| 11 | 199 | 18516 | MATCH |
| 12 | 139 | 641960 | MATCH |
| 12 | 199 | 494183 | MATCH |
| 12 | 211 | 426537 | MATCH |

**Timing vs. baseline** (load average 8-14 at measurement time):

| K | P | baseline | item 6 | Δ |
|---|---|---|---|---|
| 10 | 127 | 0.109s | 0.052s | -52.3% |
| 10 | 199 | 2.881s | 0.875s | -69.6% |
| 10 | 461 | 116.930s | 45.553s | -61.0% |
| 11 | 131 | 0.541s | 0.195s | -63.9% |
| 11 | 199 | 11.090s | 3.349s | -69.8% |
| 12 | 139 | 6.273s | 3.493s | -44.3% |
| 12 | 199 | 68.219s | 22.958s | -66.3% |
| 12 | 211 | 115.294s | 41.165s | -64.3% |

44-70% faster than baseline on every case — the largest win of any item so
far, matching the plan's "Large" impact rating.
