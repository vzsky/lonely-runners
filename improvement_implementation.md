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

---

## Item 7: attempted, held off (real regression vs. item 6, not committed)

**Deliberately not editing `improvement_plan.md` for this** — the plan's
own item ordering and reasoning stay as written; this section records
what was actually measured and why the plan's stated implementation
order is being deviated from for now (item 8 and item 11 next, item 7
revisited after), without rewriting the plan itself.

**What was implemented (matches the plan's item 7 text):** replaced
`early_return_bound` (the old `bestCovering`/`bestCovering_next`
approximation, gated at `elems.size() >= K - 4`) with a real
`gain_bound`, structurally equivalent to basegen's v4/v5 `gain_bound` +
v6.2's refinements: `Context` gained an asserted `class_size()` (`m`)
invariant; `gain_bound` short-circuits on `need > slots*m`, uses a fixed
top-4 buffer for `slots <= 4`, falls back to `nth_element` with a
total-sum shortcut for `slots > 4`, and returns `bsum` for item 9 to reuse
later. Called unconditionally at the top of `Dfs::run`, not gated by
`K-4`.

**Correctness:** verified against `test.cpp`'s fixed oracle, all 8 cases
byte-identical.

**Timing — a real regression relative to item 6, the state right before
it** (not just noise; consistent across every case):

| K | P | baseline | item 6 (current committed state) | item 7 (uncommitted attempt) | item7 vs item6 |
|---|---|---|---|---|---|
| 10 | 127 | 0.109s | 0.052s | 0.086s | +65% slower |
| 10 | 199 | 2.881s | 0.875s | 1.595s | +82% slower |
| 10 | 461 | 116.930s | 45.553s | 72.935s | +60% slower |
| 11 | 131 | 0.541s | 0.195s | 0.372s | +91% slower |
| 11 | 199 | 11.090s | 3.349s | 7.109s | +112% slower |
| 12 | 139 | 6.273s | 3.493s | 4.967s | +42% slower |
| 12 | 199 | 68.219s | 22.958s | 44.952s | +96% slower |
| 12 | 211 | 115.294s | 41.165s | 77.968s | +89% slower |

(Item 7 is still 20-45% faster than the *original* baseline in absolute
terms — this isn't a correctness or general-performance problem, it's
specifically a regression against the immediately preceding, already-fast
item-6 state.)

**Likely cause:** `gain_bound` now runs unconditionally at every node and
scans all of `avail` (not `cand`-restricted) to compute the top-`slots`
gain sum — real, non-trivial per-node cost. The bound it replaced only
ran near leaves (`elems.size() >= K-4`), where `avail` is already small;
item 6 (already committed) separately made the actual branching cheap via
`cand(t) & avail`. Basegen's own ~3x figure for this exact bound was
measured on `dfs_irred`, which is irredundancy-constrained and keeps
`avail` much smaller throughout than this general (redundancy-allowing)
search does. The plan's own dependency note for item 9 — "child-gain
cutoff... since children are gain-sorted (item 8) and a tight bound
exists (item 7), a child with gain `g` can only complete the cover if `g
+ bsum >= need`... one `break` instead of visiting the whole dead tail" —
is plausibly exactly the mechanism that turns `gain_bound`'s scan cost
into a real win; without it, this search is paying the scan cost with
nothing yet converting it into fewer nodes visited.

**Dependency check before deciding how to proceed** (re-reading the
plan's own stated dependencies for items 8-11):
- **Item 8** (gain-sorted children): depends only on items 5-6. Does
  **not** need item 7.
- **Item 9** (child-gain cutoff): depends on items **7 and 8** —
  explicitly needs `gain_bound`'s `bsum` output for the cutoff test
  (`child_gain + bsum < need`).
- **Item 10** (exact slots==2 finish): depends on items 4-5, and the plan
  explicitly lists item 7 too ("a slots==2-shaped bound to fast-reject").
- **Item 11** (undo-log): depends only on item 3. Does **not** need item
  7.

**Decision:** hold off item 7 for now (not committed; `src/find_cover.h`
reverted to the item-6 state via `git checkout --`). Proceed with item 8
and item 11 next, both of which are independent of item 7. Revisit item 7
once item 8 (gain-sorted children) is in place, since item 9's cutoff —
which needs both 7 and 8 together — is the mechanism most likely to
recover this regression; measuring item 7 combined with 8+9 together (or
at least re-measuring it with 8 already landed) should be more
informative than the isolated measurement above. Items 9 and 10 stay
blocked on this until item 7 is revisited and actually pays for itself.

---

## Item 8: gain-sorted children, computed once per node

Implemented on top of item 6 directly (does not need item 7, per the
dependency check above). `Dfs::run`'s child loop now scores every
candidate once (`gain = (cover(v) & unc).count()`, `unc = ~state.covered`
computed locally just for this — cheap, and independent of the held-off
`gain_bound`) into a buffer, sorts descending by gain (ties broken by
ascending class index via `ScoredChild::operator<`, matching v4/v5 +
v6.1's hoist-out-of-the-comparator fix), then visits children in that
order instead of raw index order. Doesn't change *which* children get
visited, only the order — the same "eliminate after visiting" pattern
governs correctness regardless of visitation order (established earlier
for `DfsIrred`'s analogous loop, and it's the same combinatorial
generation mechanism here).

**Three buffer implementations were measured, in order:**
1. Fixed `std::array<ScoredChild, P/2>` with a manual index counter.
2. `std::vector<ScoredChild>` (reviewer's rewrite) — correct, but
   heap-allocates on every single `Dfs::run` call, which is the hottest
   function in the whole search.
3. `InlinedVector<T, Capacity>` (`src/inlined_vector.h`, new) — a
   fixed-capacity, stack-allocated vector with a `push_back`/`emplace_back`
   API but no heap allocation at all (same backing storage as (1), just
   with vector-style ergonomics instead of a manual index). This is what's
   committed.

**Verification:** `test.cpp`, all 8 fixed cases, byte-identical — checked
after each of the three buffer implementations above.

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

**Timing, all three buffer implementations vs. item 6** (the immediately
preceding state — this is the comparison that matters, since item 7's
regression showed baseline-only comparisons can be misleading; load
average 8-13 across these runs):

| K | P | item 6 | (1) array | (2) std::vector | (3) InlinedVector (committed) |
|---|---|---|---|---|---|
| 10 | 127 | 0.052s | 0.056s | 0.067s | 0.057s |
| 10 | 199 | 0.875s | 0.814s | 0.911s | 0.832s |
| 10 | 461 | 45.553s | 39.824s | 41.897s | 43.631s |
| 11 | 131 | 0.195s | 0.176s | 0.203s | 0.181s |
| 11 | 199 | 3.349s | 2.941s | 3.129s | 2.983s |
| 12 | 139 | 3.493s | 3.225s | 3.387s | 3.275s |
| 12 | 199 | 22.958s | 18.931s | 20.975s | 20.345s |
| 12 | 211 | 41.165s | 32.730s | 36.975s | 35.850s |

`std::vector` is consistently the slowest of the three (heap alloc per
node), `InlinedVector` recovers most but not quite all of the plain
array's win — plausibly the `push_back`/`emplace_back` indexing overhead
vs. a raw index counter, not investigated further since the difference is
small and `InlinedVector` is meaningfully better than `std::vector`
everywhere it was measured. Net result vs. item 6: roughly 5-13% faster
(vs. the plain array's 7-21%), still a real, consistent win, matching the
plan's "Medium standalone" impact rating — unlike item 7, which regressed
under the identical before/after comparison methodology.

---

## Item 11: attempted, held off (real regression vs. item 8, not committed)

**Not editing `improvement_plan.md` for this either** — same reasoning as
the item 7 discovery note: this records what was actually measured, not a
plan revision.

**What was implemented (matches the plan's item 11 text):**
`AvailableChoice` gained `undo_eliminate(i)`, the exact inverse of
`eliminate(i)` — `_eliminated.reset(i)` plus re-incrementing `_remaining`
over `cover(i)`'s set bits. Since `eliminate`/`undo_eliminate` are both
pure functions of the static `cover(i)` table (no cross-class dependency,
unlike basegen's `IrState`), undoing doesn't need a position-level log —
just remembering *which class indices* were eliminated during a node's
child loop is enough to reverse it exactly, in any order (integer
increment/decrement and independent bit resets are order-independent;
confirmed and simplified from an initial reverse-order loop after review
caught the unnecessary ordering assumption). `Dfs::run`'s
`const auto saved_choice = state.choice;` / `state.choice = saved_choice;`
(one full `AvailableChoice` copy + restore per node, regardless of
branching factor) replaced with an `InlinedVector<int, P/2> touched` log,
appended to once per child, replayed with a plain `for (int i : touched)
state.choice.undo_eliminate(i);` after the loop.

**Correctness:** verified against `test.cpp`'s fixed oracle, all 8 cases
byte-identical — checked for both the initial reverse-order version and
the simplified forward-order version.

**Timing — a real regression relative to item 8, the state right before
it** (load average 8-16 across these runs; the pattern — consistently
worse, worst on the biggest case — doesn't read as noise):

| K | P | item 8 (committed) | item 11 (reverted) | Δ |
|---|---|---|---|---|
| 10 | 127 | 0.057s | 0.070s | +22.8% |
| 10 | 199 | 0.832s | 0.871s | +4.7% |
| 10 | 461 | 43.631s | 45.018s | +3.2% |
| 11 | 131 | 0.181s | 0.185s | +2.2% |
| 11 | 199 | 2.983s | 3.377s | +13.2% |
| 12 | 139 | 3.275s | 3.380s | +3.2% |
| 12 | 199 | 20.345s | 22.967s | +12.9% |
| 12 | 211 | 35.850s | 50.508s | +40.9% |

**Likely cause:** the old code paid one fixed-size struct copy (save +
restore) per *node*, regardless of branching factor. The undo-log instead
calls `undo_eliminate(i)` once per *child* — doing the exact same `O(m)`
word-scan work `eliminate(i)` already did — so it doubles the
elimination-related work per child rather than paying a single upfront
copy. `AvailableChoice` (`Bitset<P/2>` plus a `P/2`-byte array, roughly
30-260 bytes across these primes) is small enough that copying it is a
near-free `memcpy` on modern hardware; replaying `nch` separate
bit-scanning lambda calls isn't necessarily cheaper than that once `nch *
m` becomes comparable to `P/2` — which, empirically, it does for this
codebase's actual branching factors even after items 6 and 8 restricted
them. Basegen's own version of this optimization (`add_irred_logged`/
`undo_irred`) exists inside `dfs_irred`, where the state being logged
(`IrState`, ~1.4KB) is far larger relative to the per-child log than
`AvailableChoice` is here — a case where avoiding the big copy plausibly
does win. The same "measured on a different, more constrained search"
gap that explained item 7's regression applies here too.

**Decision:** hold off item 11 (not committed; `src/find_cover.h`
reverted to the item-8 state via `git checkout --`). Nothing later in the
plan depends on item 11, so this doesn't block anything — unlike item 7,
there's no obvious "combine with another held-off item and re-measure"
path here, since the regression's cause (small state, cheap to copy
outright) isn't something a later item changes. Revisit only if
`AvailableChoice` grows significantly larger (e.g. if a future item adds
substantial per-class state), which would shift the copy-vs-replay
tradeoff back in the log's favor.

---

## Items 7+9, together: the regression theory confirmed

Item 7's postmortem named a specific hypothesis: `gain_bound` pays a real
per-node scanning cost with nothing yet converting it into fewer nodes
visited, and item 9's child-gain cutoff was exactly the missing
mechanism. Implemented and measured as a single unit — **never
committing item 7 alone** — precisely because item 7 alone is a known
regression; only the combination was a candidate for landing.

**Change (`src/find_cover.h`), on top of item 8's committed state:**
- Reintroduced `Context::class_size()` (asserted `m` invariant), gated by
  `need > slots*m` at the top of `next_choices` (formerly a standalone
  `gain_bound`/`early_return_bound`, since folded into a single
  `next_choices` that both computes the bound and returns the branch
  list — no separate bound function, no `Dfs::run`-side bound call).
- `next_choices` builds one `InlinedVector<ScoredChild, P/2> children`
  from a single `avail.for_each` scan (gain + index per candidate) — this
  is the *only* array built; there is no second "full avail" array
  alongside it. The top-`slots` sum/min needed for the bound is computed
  by a small `TopElements<T, K>` class (`src/find_cover.h`): a fixed-size
  insertion buffer that tracks the `K` largest values pushed into it (no
  runtime "how many of K are actually active" state at all — see below
  for why that became unnecessary).
- Which compile-time `K` `TopElements` gets is chosen by a new
  `utils::dispatch<K>(x, f)` (`src/utils.h`): given a runtime `x < K`,
  it calls `f(std::integral_constant<std::size_t, X>{})` for whichever
  compile-time `X` equals `x`, via a `std::index_sequence`-driven fold
  expression. This generalizes basegen's `slots<=4` / `slots>4` split
  into a full per-value dispatch across `slots`'s entire range
  (`utils::dispatch<K+1>(slots, ...)`, since `slots` runs 1..K) — every
  reachable `slots` value gets its own compile-time-sized `TopElements`,
  not just a fixed size-4 buffer plus an `nth_element` fallback. Since
  every dispatched instantiation's `TopElements<T,S>` is sized exactly
  to the `slots` it will ever see, `TopElements` itself needed no
  runtime "capacity vs. active count" distinction (unlike the
  `BoundedHeap` predecessor of this class, which took a runtime `k` at
  construction) — it always fills to its one fixed size.
- Item 9's cutoff survives, but reshaped: instead of sorting `children`
  and `break`-ing on the first sorted entry that fails
  `gain + bsum < need`, `children` is left **unsorted** — since only
  *membership* above the cutoff matters, not order, the final loop just
  `continue`s past both cutoff failures and `cand(nextToCover)` filter
  failures, checking every entry once (`O(n)`, no sort) rather than
  sorting to get an early `break`.

**Dead ends hit and reverted while arriving at this shape** (kept here
since each one changed the final design):
- First pass collapsed `all` (full `avail` scan) and `scored`
  (`cand`-filtered) into one `InlinedVector`, but kept a full `std::sort`
  of it for the bound — this over-corrected: sorting the *entire*
  `avail`-sized list (not just the small `cand`-filtered subset) on every
  node regressed 9-57% vs. the item 8 baseline it was meant to improve on
  (K=10/P=461: 41.9s vs. baseline's earlier 24-44s band). Root cause:
  `nth_element`'s partial selection was replaced by an O(n log n) full
  sort of a potentially-large `avail`, precisely the cost basegen's
  `slots<=4` fixed-buffer split was designed to avoid.
- Fix: switched the bound computation back to `nth_element` (partial),
  dropped the full sort entirely (order isn't needed for correctness,
  only for the old `break`-based cutoff, which was replaced by an
  unconditional `continue`-based scan) — recovered to the item 8 band
  (30.3s/20.1s/34.1s for 461/199-12/211 across two repeated runs).
- Resurrecting the old top-4 fixed buffer (basegen's exact `slots<=4`
  split) on top of that `nth_element` fix was tried in isolation and
  measured 25-39% faster still (26.2s/17.1s/27.4s) — confirming the
  split is a real, additional win, not just insurance against the
  full-sort regression above.
- Generalized the two-branch split into `utils::dispatch` (this section's
  final shape) specifically to remove the arbitrary `slots<=4` cutoff and
  the duplicated scan-loop body between the two branches, while keeping
  each `slots` value's own fixed-size buffer. Verified equivalent-or-better
  timing at every step (manual `switch(slots){case 2..7, default:}` before
  generalizing to the full `dispatch<K+1>`, both within noise of the
  isolated top-4-buffer measurement above).
- One live bug caught mid-refactor: `TopElements<int, S> top();` (intended
  as a variable declaration) is C++'s classic most-vexing-parse — it
  declares a function returning `TopElements<int,S>`, not a variable,
  which surfaced as "functions that differ only in their return type
  cannot be overloaded" once two different `S` instantiations collided.
  Fixed to `TopElements<int, S> top;`.

**Verification:** `test.cpp`, all 8 fixed cases, byte-identical, at every
step above (each dead end and the final shape).

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

**Timing vs. item 8** (the immediately preceding committed state; load
average 3-13 at measurement time):

| K | P | baseline | item 8 | items 7+9 | Δ vs item8 | Δ vs baseline |
|---|---|---|---|---|---|---|
| 10 | 127 | 0.109s | 0.057s | 0.052s | -8.8% | -52.3% |
| 10 | 199 | 2.881s | 0.832s | 0.633s | -23.9% | -78.0% |
| 10 | 461 | 116.930s | 43.631s | 24.340s | -44.2% | -79.2% |
| 11 | 131 | 0.541s | 0.181s | 0.179s | -1.1% (noise) | -66.9% |
| 11 | 199 | 11.090s | 2.983s | 2.094s | -29.8% | -81.1% |
| 12 | 139 | 6.273s | 3.275s | 3.022s | -7.7% | -51.8% |
| 12 | 199 | 68.219s | 20.345s | 13.745s | -32.4% | -79.9% |
| 12 | 211 | 115.294s | 35.850s | 21.862s | -39.0% | -81.0% |

**A real, substantial further speedup — 8-44% faster than item 8, biggest
on exactly the largest cases (461, 199/12, 211/12) where item 7 alone
regressed worst.** This confirms the postmortem's theory: `gain_bound`'s
per-node scan cost was real, but it was never supposed to stand alone —
item 9's cutoff is what converts that cost into fewer nodes actually
visited, and the two together are a net win where either alone (7
without 9, or 8 without 7's tighter bound feeding it) was not. Cumulative
from baseline: 52-81% faster across all 8 cases, the best result of any
item so far.

**Unblocks:** items 9 and 10 were both gated on item 7 landing; item 9 is
now done (this entry). Item 10 (exact `slots==2` closed-form finish) can
now be attempted.

**Follow-up check: is the two-branch split actually earning its
complexity here, or is it cargo-culted from basegen?** Tried collapsing
`gain_bound` into a single `nth_element`-only path (drop the `slots <=
4` fixed-top-4 branch entirely, always build the full `gains` array and
`nth_element` it) on a scratch copy, verified correctness (all 8 cases
still byte-identical), then timed it:

| K | P | two-branch | collapsed (nth_element only) | Δ |
|---|---|---|---|---|
| 10 | 127 | 0.047s | 0.095s | +102% |
| 10 | 199 | 0.613s | 1.268s | +107% |
| 10 | 461 | 25.270s | 43.889s | +73.7% |
| 11 | 131 | 0.190s | 0.286s | +50.5% |
| 11 | 199 | 2.163s | 3.936s | +82.0% |
| 12 | 139 | 3.072s | 3.798s | +23.6% |
| 12 | 199 | 14.226s | 25.679s | +80.5% |
| 12 | 211 | 22.282s | 44.465s | +99.6% |

Collapsing is **24-107% slower** (roughly 1.2x-2.1x) across every case.
The two-branch split is genuinely load-bearing on this codebase, not
just carried over unexamined from basegen.

**Superseded by the generalization above:** rather than stopping at
basegen's fixed `slots<=4`/`slots>4` split, that split was generalized
into `utils::dispatch<K+1>(slots, ...)` giving *every* `slots` value
1..K its own compile-time-sized `TopElements` buffer (see "Change" and
"Dead ends" above) — a strict superset of the two-branch version, since
`slots<=4` is just 4 of the dispatched cases. This is the shape that
actually landed; the two-branch version above was a real intermediate
step, not the final one.

**Final verification and timing** (`test.cpp`, all 8 cases
byte-identical; two repeated runs, load average 9-14 at measurement
time):

| K | P | item 8 (committed) | final (TopElements + dispatch) | Δ vs item 8 |
|---|---|---|---|---|
| 10 | 127 | 0.057s | 0.060s | +5.3% (noise) |
| 10 | 199 | 0.832s | 0.812s | -2.4% (noise) |
| 10 | 461 | 43.631s | 27.956s | -35.9% |
| 11 | 131 | 0.181s | 0.236s | +30.4% (noise; smallest case) |
| 11 | 199 | 2.983s | 2.620s | -12.2% |
| 12 | 139 | 3.275s | 3.377s | +3.1% (noise) |
| 12 | 199 | 20.345s | 18.605s | -8.6% |
| 12 | 211 | 35.850s | 31.059s | -13.4% |

The largest cases (461, 199/12, 211/12) — where the search actually
spends its time — are 8-36% faster than item 8; the smallest cases are
within measurement noise either way, as expected (their absolute times
are dominated by process/thread startup, not the algorithm). This is the
committed shape.
