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
