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
