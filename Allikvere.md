# Porting Allikvere's `basegen` optimizations into `find_cover.h`

This is entirely Claude Sonnet 5's port of the source listed below. 
I (Touch Sungkawichai) have made some effort to verify the implementation in this branch and guide it 
to make prettier choices. I *think* this is correct. I am not claiming anything about the validity of implementation in the sources.
They, however, contain some useful idea that let Claude improve run time of this implementation.
Ultimately, I think this implementation could be understood not so difficultly and can be used as a base for anyone interested to tackle this problem. 

The rest of this page is written by Claude. 

---

A compiled, final record of what was ported from Jaan Allikvere's *Fourteen
lonely runners* and its companion code into this repo's `src/find_cover.h`.
This supersedes two now-retired working documents that existed only for
the duration of the porting work: `improvement_plan.md` (the original,
item-by-item porting plan) and `improvement_implementation.md` (the
step-by-step implementation log — every dead end, every timing run, every
piece of reasoning behind the numbers quoted below). Both were deleted
once this file was compiled from them; this is the sorted, final summary
that replaces them, not a companion to them.

## Sources

- **Paper:** Jaan Allikvere, *Fourteen lonely runners: manuscript, gate
  certificates, and audit code*, archived on Zenodo,
  <https://doi.org/10.5281/zenodo.22066772> (CC BY 4.0). Read directly for
  the real 111-prime gate list (Table `tab:gates`, §6) and the paper's own
  reported computational cost, rather than relying on this repo's own
  (smaller, older) `main.cpp::LrcVerifier<13>::Primes` list. (Note: the
  paper itself cites `arXiv:2604.23906` — T. Sungkawichai and
  T. Trakulthongchai, *Eleven, twelve, and thirteen lonely runners*
  (2026) — as prior work its own numbering follows; that ID belongs to
  that separate paper, not to Allikvere's.)
- **Code:** `../fourteen_lonely_runners/basegen_v5.cpp` and
  `basegen_v6.cpp` — copies of the same-named files confirmed present in
  the Zenodo archive above (`CODE_GUIDE.md` from that archive describes
  `basegen_v5.cpp` as "the audited baseline. Ran the small-prime
  campaign" and `basegen_v6.cpp` as "v5 with capacity widening only",
  matching these files' own header comments exactly). Per those header
  comments, the algorithm lineage is **v4 == v5** (v5 is "the audited v4
  ... with exactly two capacity fixes, no algorithmic changes"), then
  **v6.1 → v6.2 → v6.3 → v6.4** (in-code comments) layer real
  algorithmic work on top, and **v6 proper** adds only capacity widening.
  `../fourteen_lonely_runners/basegen_trimmed.cpp` (a hardcoded-K=13,
  v5-capacity reference build of the same algorithm) was used later for
  direct timing comparisons.
- **Target:** `src/find_cover.h` — this repo's only structural analog to
  `basegen`'s search.

Every item below is something that actually exists in `basegen`'s source or
the paper's text, not an invented technique; each names the version that
introduced it. Full derivations, dead ends, and every timing run were
tracked in the now-retired `improvement_implementation.md` during
development — this file gives the final shape and the headline numbers
only.

---

## Adopted, in implementation order

Validated at every step during development against a fixed 8-case oracle
(K=10/P=127,199,461; K=11/P=131,199; K=12/P=139,199,211; a self-contained
sha256-of-canonical-output check, since deleted along with the other
working documents above), byte-identical throughout. Baseline
(pre-optimization) timings for comparison:

| K | P | count | baseline time |
|---|---|---|---|
| 10 | 127 | 8228 | 0.109s |
| 10 | 199 | 4417 | 2.881s |
| 10 | 461 | 1 | 116.930s |
| 11 | 131 | 40615 | 0.541s |
| 11 | 199 | 18516 | 11.090s |
| 12 | 139 | 641960 | 6.273s |
| 12 | 199 | 494183 | 68.219s |
| 12 | 211 | 426537 | 115.294s |

### 1. Custom `Bitset<N>` type (`src/bitset.h`)

**Origin:** v4/v5's `Bits` type (`struct Bits { uint64_t w[NW]; }` with
`set`/`reset`/`test`/`count`/`any` and free bitwise operators) — v4 used
128 bits, v5 widened to 256 (a capacity fix, not algorithmic).

Replaced `std::bitset<P/2>` (no word-level access in libc++) and
`AvailableChoice`'s `std::array<char, P/2>` (a linear-scan "eliminated"
array, not a bitset at all) with one fixed-word `Bitset<N>` type used for
both roles. **Impact:** enabler — foundational for every item below, not a
standalone win (first isolated timing pass was inconclusive/noise-level, as
expected for a pure infrastructure change).

### 2. Transposed per-point candidate table `cand[point]`

**Origin:** v4/v5's `cand` vector (present since v4): alongside
`cov[class] → points it covers`, also store `cand[point] → classes that
cover it`.

Added `Context::mCand` alongside the existing `mCover`, filled in the same
constructor pass. Purely additive on its own (nothing read it yet) — the
payoff comes from item 4. **Impact:** enabler for items 4 and the
slots==2 finish (item 7 below).

### 3. Word-scan bit iteration

**Origin:** v6.1: every hot loop that walks a `Bits`'s set bits switches
from "test every index" to `while(w){ t = 64*j + ctzll(w); w &= w-1; }` —
cost proportional to popcount, not to `n`.

Added `Bitset<N>::for_each` (a `const` member, not basegen's free
function — reworked after review so `Bitset`'s internals could stay
private) and applied it to `AvailableChoice`'s constructor, `eliminate()`,
and `get_next_to_cover`.

**Impact — real, 25-45% faster** on every non-trivial case:

| K | P | baseline | this item | Δ |
|---|---|---|---|---|
| 10 | 199 | 2.881s | 1.800s | -37.5% |
| 10 | 461 | 116.930s | 63.912s | -45.3% |
| 11 | 199 | 11.090s | 6.363s | -42.6% |
| 12 | 199 | 68.219s | 41.735s | -38.8% |
| 12 | 211 | 115.294s | 78.920s | -31.6% |

### 4. Restrict candidate enumeration to `cand(t) & avail`

**Origin:** v4/v5: `choices = cand[bt] & avail; for v in choices ...`
instead of scanning every class and filtering.

`Dfs::run`'s main loop and the bound's "which classes cover the target
point" computation both switched from scanning all `P/2` classes to
intersecting `cand(nextToCover)` with `avail` directly (`AvailableChoice`
gained an `available()` accessor for this).

**Impact — the single largest win of any item, 44-70% faster** than
baseline:

| K | P | baseline | this item | Δ |
|---|---|---|---|---|
| 10 | 127 | 0.109s | 0.052s | -52.3% |
| 10 | 199 | 2.881s | 0.875s | -69.6% |
| 10 | 461 | 116.930s | 45.553s | -61.0% |
| 11 | 199 | 11.090s | 3.349s | -69.8% |
| 12 | 199 | 68.219s | 22.958s | -66.3% |
| 12 | 211 | 115.294s | 41.165s | -64.3% |

### 5. Gain-sorted children, computed once per node

**Origin:** v4/v5 (sorting candidates by descending gain before
recursing) + v6.1 (hoisting the gain computation out of the sort
comparator, instead of recomputing it O(k log k) times per node).

Each candidate's gain (`(cover(v) & unc).count()`) is computed once into a
buffer, sorted descending, then visited in that order. Three buffer
implementations were tried (fixed array, `std::vector`, and a new
`InlinedVector<T,Capacity>` — a fixed-capacity, stack-only vector type,
`src/inlined_vector.h`); `InlinedVector` was kept as the best
heap-allocation-free option with vector-style ergonomics.

**Impact — a further 5-13% faster** than item 4 alone (`std::vector`
measured consistently worst of the three, due to a heap allocation on
every single node).

### 6. Tightened gain bound + child-gain cutoff (items 7+9, landed together)

**Origin:** v4/v5's `gain_bound` (sum of top-`slots` gains, pruning if
`< need`) + v6.2's refinements (`need > slots*m` O(1) short-circuit, a
fixed top-4 buffer for `slots<=4` instead of `nth_element`, a total-sum
shortcut for `slots>4`) + v6.3's child-gain cutoff (`break` once a
gain-sorted child can't reach `need` even combined with the bound on the
rest, since every later child fails too).

**The single largest documented win in basegen (~3x on their own
benchmark) — but landing it required a real fix first.** Implemented as a
standalone bound (item 7 alone), it was a **genuine regression** against
item 5 (42-112% slower) — the bound's per-node scan cost was real, but
without the cutoff (item 9) to convert that cost into fewer nodes
actually visited, it was pure overhead. Combining both fixed this
entirely and then some. Along the way, basegen's fixed `slots<=4` /
`slots>4` split was generalized into `utils::dispatch<K>(x, f)`
(`src/utils.h`): given a runtime `x < K`, it calls
`f(std::integral_constant<std::size_t, X>{})` for the matching
compile-time `X` via an index-sequence fold — every `slots` value from 1
to K gets its own compile-time-sized top-K buffer (`TopElements<T,K>`),
not just a hardcoded top-4 case. Verified this split is genuinely
load-bearing here, not cargo-culted: collapsing it back to a single
`nth_element`-only path measured 24-107% slower.

**Impact — 52-81% faster than baseline, the best result of any single
item:**

| K | P | baseline | this item | Δ vs baseline |
|---|---|---|---|---|
| 10 | 127 | 0.109s | 0.060s | -45.0% |
| 10 | 199 | 2.881s | 0.812s | -71.8% |
| 10 | 461 | 116.930s | 27.956s | -76.1% |
| 11 | 131 | 0.541s | 0.236s | -56.4% |
| 11 | 199 | 11.090s | 2.620s | -76.4% |
| 12 | 139 | 6.273s | 3.377s | -46.2% |
| 12 | 199 | 68.219s | 18.605s | -72.7% |
| 12 | 211 | 115.294s | 31.059s | -73.1% |

### 7. Exact closed-form finish at `slots == 2`

**Origin:** v6.2 (`gain_bound`'s slots==2 branch in `dfs_irred`) — basegen's
single biggest *measured* win on their own benchmark, 373s → 125s (~3.0x).

When exactly 2 picks remain, the first still goes through the normal
bound/cutoff, but the second is computed directly as the intersection of
`cand(t)` over every point still uncovered after the first pick — no third
level of branching. Verified algebraically, not just empirically, that
this is exactly equivalent to what the generic path would compute at
`slots==1` (its cutoff term reduces to precisely "covers every remaining
point" when points remain, and to "always true" — any available class —
when none do, repeats included, since a cover here is a multiset of `K`
picks, not a set of `K` distinct classes).

**Impact — a real, modest 2-21% win** on every case:

| K | P | previous | this item | Δ |
|---|---|---|---|---|
| 10 | 127 | 0.060s | 0.050s | -16.4% |
| 10 | 461 | 27.956s | 27.378s | -2.1% |
| 11 | 131 | 0.236s | 0.188s | -20.5% |
| 12 | 199 | 18.605s | 16.935s | -9.0% |
| 12 | 211 | 31.059s | 28.801s | -7.3% |

Nowhere near basegen's own ~3x on this exact optimization — expected, not
a red flag: basegen measured it inside `dfs_irred` (irredundancy-
constrained, where the last-two-picks case is a much larger share of
total nodes); here it's one shortcut inside the general
(redundancy-allowing) search, so its relative contribution is smaller by
construction.

### 8. Redundancy decomposition (`fill_free_slots`) — generalizing item 7

**Origin:** the *identity* behind v4/v5's whole irredundant/redundant
search split (`run_decomposed`): every complete cover is either
irredundant, or a smaller complete cover plus arbitrary padding. Not a
literal port of basegen's split architecture (see "Tried, not adopted"
below for why) — a from-scratch generalization of item 7's own closed-form
finish, arrived at by recognizing that basegen's decomposition identity is
already latent in this repo's own search structure.

Item 7's "exactly 2 remaining" special case is generalized to *any*
number of remaining slots: `SpeedSet<K>::fill_free_slots(prime)`
(`src/speedset.h`) fills whatever's left with any speed (repeats
included), the moment `nextToCover` becomes absent (coverage already
complete), instead of continuing the bound-restricted search. `run()`
checks this before the `slots==2` special case; `finish_last_two` itself
now delegates to `fill_free_slots` for its own "first pick already
completes coverage" branch instead of a separate manual loop.

**Why this needs no `IrState`-style essentiality tracking at all** (unlike
basegen's `dfs_irred`): every pick made while `next_choices` still has an
uncovered point to satisfy is already locally necessary by construction
(it's drawn from `cand(nextToCover) & avail`, and `nextToCover` is by
definition uncovered, so the pick has `gain >= 1` the moment it's chosen).
Once coverage completes, *any* padding is a valid answer regardless of
whether the picks that got there were technically irredundant — the
irredundant/padded split only needs to be exhaustive, not filtered. So one
unified DFS naturally discovers whatever support size coverage happens to
complete at, on every branch independently, and pads exactly that
branch's remaining slots right there — with none of basegen's
`add_irred_logged`/`undo_irred` bookkeeping, and none of the standalone
decomposition attempt's overhead (see below).

**Impact — `finish_last_two` (now folded into this) measured 6-17% faster
than without it**, A/B-tested directly on top of the fully-optimized
state:

| K | P | with finish_last_two | without | Δ |
|---|---|---|---|---|
| 10 | 461 | 29.287s | 31.192s | -6.1% |
| 11 | 199 | 2.660s | 2.917s | -8.8% |
| 12 | 139 | 3.466s | 3.941s | -12.1% |
| 12 | 199 | 18.302s | 20.665s | -11.4% |
| 12 | 211 | 31.637s | 38.237s | -17.3% |

**Current committed timings, full 8 cases** (this is the state on disk
today):

| K | P | count | time |
|---|---|---|---|
| 10 | 127 | 8228 | 0.065s |
| 10 | 199 | 4417 | 0.763s |
| 10 | 461 | 1 | 29.287s |
| 11 | 131 | 40615 | 0.226s |
| 11 | 199 | 18516 | 2.660s |
| 12 | 139 | 641960 | 3.466s |
| 12 | 199 | 494183 | 18.302s |
| 12 | 211 | 426537 | 31.637s |

**Not yet ported from basegen's own redundant-half trick:**
`fill_free_slots`'s loop tries every speed at every remaining slot
unconstrained, generating every *ordered* sequence rather than every
*multiset* — i.e. it explores `(P/2)^remaining_slots` raw branches when
only `C(P/2 + remaining_slots - 1, remaining_slots)` distinct results
exist (smaller by roughly `remaining_slots!`). Basegen's
`compositions_rec` (distributing a fixed coordinate budget across a
support via multiplicities, never re-deriving orderings) is exactly the
applicable fix, transferable as: constrain the loop to non-decreasing
values. Identified but not yet implemented or measured.

---

## Tried, not adopted (kept for the record, not because they're wrong to revisit)

### The literal irredundant/redundant search split (v4/v5's `run_decomposed`)

**Origin:** basegen's actual architecture — search `enumerate_irredundant13`
(a dedicated `dfs_irred` tracking per-class private points via `IrState`)
and `enumerate_12covers`+`reducible_from_12` (complete covers using *at
most 12* coordinate slots, via `compositions_rec` distributing
multiplicities across a smaller support) separately, union the results.

**Why it doesn't port as a literal architecture:** `find_cover.h`'s
`SpeedSet<K>` has no multiplicity concept — a redundant K-cover here can
have a minimal sub-cover of *any* size 1..K-1, not always exactly K-1 like
basegen's fixed-at-12-coordinates trick guarantees, so a literal
"(K-1)-covers, extend by one" port would silently miss solutions. A first
implementation attempt (commit `b652bb9` on an earlier iteration of this
branch) hit a real overlap bug and a 10-22x wall-clock regression; it was
discarded by request rather than debugged further at the time.

**Revisited later this session as a standalone `DfsIrred<P,K>`**
(parameterized by a runtime target support size `s`, called once per `s`
from 1 to K, unioned with padding) — validated correct (matched the
oracle exactly at every step) but consistently **1.1-2.6x slower** than
the unified general search even after porting essentiality-tracking
(`IrState`-equivalent `try_add`/`undo_add`), the `need>=slots` bound, and
class-0 symmetry fixing at every support level. Root cause, confirmed by
direct reasoning rather than further guessing: the standalone search pays
real overhead (per-level re-derivation, no cross-level dedup, an
unnecessary O(s²) leaf essentiality check that turned out to not even be
required for correctness) to compute something the unified search already
gets for free — see item 8 above, which is what actually replaced this
attempt. Not committed; superseded entirely by item 8's fusion.

### Item: `avail.count() < slots` / `need >= slots` short-circuit

**Origin:** v4/v5 (`avail.count() < slots`) and v6.4 (`need >= slots`,
irredundancy-specific).

Only the `avail.count() < slots` half is even valid to port to the general
search (the `need >= slots` half relies on every remaining pick being
essential, which the general search's redundant-padding-allowed leaf
condition doesn't guarantee). Implemented, verified correct, but
**instrumented and found to fire 0% of the time** across ~300M node
evaluations — `get_next_to_cover`'s own MRV branching already keeps
cumulative eliminations far short of the `P/2` vs. `K` gap this bound
would need to close. Reverted: correct-but-inert code costing constant
per-node overhead for zero pruning benefit.

### Item: undo-log instead of full `AvailableChoice` copy

**Origin:** v6.1 (`add_irred_logged`/`undo_irred` — mutate + log instead of
copying `IrState`, ~1.4KB in basegen, before every child attempt).

Implemented as `undo_eliminate(i)` plus a per-node touched-class log
replacing the full struct save/restore. Verified correct, but a **real
regression** (up to +40.9% on the largest case) against the immediately
preceding state: `AvailableChoice` here (a `Bitset<P/2>` plus a small
array, ~30-260 bytes) is cheap enough to copy outright that replaying
`nch` separate elimination calls costs more than the copy it replaces —
the opposite of basegen's situation, where the much larger `IrState` makes
avoiding the copy a real win. Revisit only if a future item makes
per-class state significantly larger.

### Item: `gain_bound` alone, without the child-gain cutoff

Covered under item 6 above — recorded separately here because it's the
one item that was a *measured regression* before being combined with its
dependency, not a bound that never fired or a copy that wasn't worth
avoiding. Never committed on its own.

---

## Not applicable

- **v6 proper's capacity widening** (wider `Bits`, `uint16_t` indices,
  64-bit quota masks): specific to basegen's `n<=897` capacity ceiling,
  which `find_cover.h`'s sizes don't hit.
- **basegen's quota-mask (`qm`) `C1` check**: specific to their
  irredundant-13 enumeration's own bookkeeping; no quota concept exists
  here.
