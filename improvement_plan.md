# Porting basegen's optimizations into `src/find_cover.h`

Source: `../fourteen_lonely_runners/basegen_v5.cpp` and
`../fourteen_lonely_runners/basegen_v6.cpp` (v6's header comment: "v6 = the
audited v5 ... with ONLY capacity widening, no algorithmic changes"; v5's
header comment: "v5 = the audited v4 ... with exactly two capacity fixes,
no algorithmic changes"). So the actual *algorithm* lineage is
**v4 == v5** (only buffer-size fixes between them, v4 itself isn't on disk
but v5's header states its content is v4 unchanged), then **v6.1 → v6.2 →
v6.3 → v6.4** (in-code comments) layer real algorithmic/constant-factor
work on top, and **v6 proper** adds only capacity widening (which doesn't
apply here — `find_cover.h` has no prime-size ceiling problem).

Target: `src/find_cover.h`. This is basegen's **only** analog in this repo
— basegen never lifts to a composite modulus, so `src/lift.h` has no
counterpart here and is out of scope.

Everything below is something that is actually in v4/v5 or v6.1-6.4's
source, not an invented technique. Each item names which version
introduced it.

---

## Part 1 — What each version actually changed (attributed)

### v4 / v5 (baseline algorithm — identical between the two)

- **Custom fixed-word `Bits` type** instead of `vector<bool>`/`set<int>`:
  a `struct Bits { uint64_t w[NW]; }` with `set`/`reset`/`test`/`count`/`any`
  and free `operator|`, `operator&`, `operator~`, `andnot`, `covers_all`.
  (v4 used 2 words / 128 bits; v5 widened to 4 words / 256 bits — a
  capacity fix, not algorithmic.)
- **Transposed candidate table**: alongside `cov[class] -> Bits of points
  it covers`, basegen also precomputes `cand[point] -> Bits of classes
  that cover it`. This is what lets every "which classes cover point t"
  query become a single `Bits` intersection (`cand[t] & avail`) instead of
  a scan over all classes.
- **`rarest_uncovered`** (MRV / minimum-remaining-values branching): pick
  the currently-uncovered point with the fewest remaining candidate
  classes (`(cand[t]&avail).count()`), with an early exit once a point
  with count `<= 1` is found (can't do better).
- **`gain_bound`**: before branching at a node, compute the gain
  (`(cov[v]&unc).count()`) of every available class, take the sum of the
  top `slots` values (`nth_element` + sum), and prune if that sum is
  `< need`. Applied at **every** node, not conditionally.
- **Gain-sorted children**: candidates are sorted by descending gain
  before recursing — `sort(ch.begin(),ch.end(),[&](a,b){ ga=(cov[a]&unc).count(); gb=(cov[b]&unc).count(); return ga!=gb ? ga>gb : a<b; })`
  — note the gain is recomputed **inside the comparator** here (O(k log k)
  popcounts), which v6.1 later fixes.
- **`avail.count() < slots` short-circuit**: if fewer candidate classes
  remain than slots to fill, the branch is dead.
- **Full-state copy per recursive attempt**: irredundance tracking
  (`IrState`, ~1.4KB) is copied wholesale (`IrState ns = state;`) before
  every child attempt so it can be discarded on backtrack. (This is the
  thing v6.1 later replaces with an undo-log.)
- **The irredundant/redundant search decomposition** (`run_decomposed`,
  cross-validated against the brute-force `run_direct` via `mode=both`):
  instead of one general DFS searching all 13-covers together, basegen
  splits into (a) `enumerate_irredundant13()` — a dedicated DFS
  (`dfs_irred`) that tracks a private/unique point per class throughout
  (`IrState`/`add_irred`) and only accepts covers where every class is
  essential, and (b) `enumerate_12covers()` + `reducible_from_12()` —
  enumerate all *complete* covers using at most 12 coordinate slots (via
  `compositions_rec`, which distributes 12 coordinate slots across a
  possibly-smaller support set using multiplicities), then extend each by
  exactly **one** more coordinate to reach 13. Every 13-cover is either
  irredundant (case a) or has a droppable class, in which case dropping it
  (and folding any resulting multiplicity) yields a complete cover of
  **exactly** 12 coordinate slots (case b) — this dichotomy is exhaustive,
  so the union of (a) and (b) is the full answer, and it's cheaper because
  (a) prunes far harder than the general bound and (b) searches a strictly
  smaller (12-slot) problem instead of the full 13-slot one.
- All of the above already use `Bits` throughout (`covered`, `avail`,
  `unc`, `choices` are all `Bits`), but iteration over them is done by
  **testing every index** (`for(int t=0;t<n;t++) if(X.test(t))`), not by
  scanning only the set bits. That word-scan trick doesn't exist yet in
  v4/v5.

### v6.1 (in-code, constant-factor pass)

- **Word-scan bit iteration**: every hot loop that walks the set bits of a
  `Bits` (`rarest_uncovered`, `gain_bound`, `ordered_choices`,
  `add_irred_logged`) switches from "test every index" to
  `while(w){ int t=64*j+__builtin_ctzll(w); w&=w-1; ...}` — cost
  proportional to popcount, not to `n`.
- **Gains hoisted out of the sort comparator**: child gains are computed
  once into a buffer (`ordered_choices`), then sorted — not recomputed
  per comparison.
- **Undo-log instead of full-state copy**: `add_irred_logged`/`undo_irred`
  mutate the irredundance-tracking state in place and log only the
  entries touched, rolling back by replaying the log — instead of copying
  the whole state struct before every child attempt.
- Measured: 1.38x on the p=239 benchmark.

### v6.2 (in-code)

- **`gain_bound`'s O(1) cardinality short-circuit**: every class covers
  exactly `m` points, so if `need > slots*m` the branch is dead — checked
  before the O(|avail|) scan.
- **Fixed-size top-k buffer for `slots <= 4`**: tracks the largest 4 gains
  in a small stack array (insertion-sort-style), replacing `nth_element`
  for the case that dominates the search (near-leaf nodes).
- **Total-sum shortcut for `slots > 4`**: if the sum of *all* available
  classes' gains is already `< need`, skip `nth_element` entirely.
- **`bsum` reuse**: `gain_bound` also returns the sum of the top
  `slots-1` gains, reused directly by the child-gain cutoff (v6.3) instead
  of being recomputed.
- **Exact closed-form finish at `slots == 2`**: instead of branching
  generically for the last two picks, compute the valid second-class set
  directly as the intersection of `cand[t]` over every still-uncovered `t`.
  Measured: 373s -> 125s (~3.0x) on the p=239 benchmark, byte-identical
  output.

### v6.3 (in-code)

- **Child-gain cutoff**: since children are gain-sorted (v4/v5) and a
  tight bound exists (v6.2), a child with gain `g` can only complete the
  cover if `g + bsum >= need`. Because children are sorted descending,
  the *first* failing child means every later child fails too — one
  `break` instead of visiting the whole dead tail. Measured: nodes 126.3M
  -> 79.6M (-37%) on p=239.

### v6.4 (in-code, undocumented in the file's header block but present in
comments)

- **`need >= slots` necessary condition**: every remaining class must end
  with a private/uncovered point, and distinct classes need distinct
  private points, so if fewer uncovered points remain than slots, the
  branch is dead — checked before the (more expensive) `gain_bound`. This
  also let v6 delete v4/v5's old "full-cover fill" branch (a special case
  for when coverage was already complete but slots remained) — it's
  provably dead code once this check exists **inside `dfs_irred`**. (Note:
  this bound is specific to the irredundant search path — `dfs_support`,
  basegen's general/non-irredundant search, keeps its own "fill" branch,
  since redundant completions are exactly what it's meant to find.)

### v6 proper (capacity widening — not portable, listed only for completeness)

- Wider `Bits` (up to 7 words), `uint16_t` class indices, and 64-bit quota
  masks (`qm`) for a `C1`-quota check specific to basegen's irredundant-13
  -cover enumeration. None of this applies to `find_cover.h`: it has no
  quota concept and its `SpeedSet<K>` / `P/2` sizes aren't hitting a
  capacity ceiling the way basegen's `n<=897` case was.

---

## Part 2 — Porting plan for `src/find_cover.h`

Item 1 (the search decomposition) is placed first by explicit choice, not
because the dependency graph forces it there — it doesn't depend on, and
isn't depended on by, items 2-11 (those are all constant-factor/pruning
work on the *existing* single-DFS structure; item 1 changes what gets
searched in the first place). Doing it first avoids re-validating all the
constant-factor items twice — once against the current single-DFS shape,
again after a later restructuring.

Items 2-11 are ordered by the actual dependency graph (what genuinely
blocks what), using impact as the tiebreak wherever nothing forces a
particular order:

- **#2** has zero dependencies and is nearly free — do it before any of
  the constant-factor items.
- **#3-#6** are infrastructure (`Bits` type, `cand` table, word-scan,
  restricted candidate iteration) that #7-#10 need in order to be fast;
  #3 must come before #4 (the table needs the bitset type), #5 needs #3,
  #6 needs #3+#4.
- **#7** (the tightened bound) only needs #3+#5 — it doesn't need children
  sorted, and it prunes whole subtrees regardless of child visit order —
  so it can land before #8 despite #8 being numerically "smaller."
- **#8** (gain-sorted children) exists specifically to enable **#9** (the
  cutoff); there's no reason to do it before #7, which delivers its (much
  larger) win independently.
- **#10** (exact slots==2 finish) only depends on #4+#5 (+ benefits from
  #7) — **not** on #11. It has *larger* impact than #11, so it's ordered
  before it, not after.
- **#11** (undo-log) depends only on #3, and nothing later depends on it.
  Since it's the lowest-impact item with no downstream dependents, it's
  correctly last — not because it's "invasive," but because nothing is
  gated on it and everything with a bigger payoff can land first.

Current relevant code (line numbers as of this writing):
- `Context` / `mCover`: `find_cover.h:23-46`
- `Dfs::State` / `run()`: `find_cover.h:53-97`
- `early_return_bound()`: `find_cover.h:100-123`
- `AvailableChoice`: `find_cover.h:196-234`

### 1. Irredundant/reducible search decomposition

**Origin:** v4/v5's overall search architecture (`run_decomposed` vs.
`run_direct`, `enumerate_irredundant13`, `enumerate_12covers` +
`reducible_from_12`) — this is the biggest, structurally different item
on the list, and the one with the most open design work.
**Impact:** Uncertain / potentially large for the irredundant half;
**basegen's specific cheap trick for the redundant half does not directly
transfer** (see below) — needs design work before its impact can be
estimated honestly.
**Effort:** High. **Depends on:** nothing (operates at the
`find_all_covers_parallel`/`Dfs` level, independent of items 2-11's
internals — though it will want *some* pruning to be worth doing well,
so revisiting after a few of 2-11 land is also reasonable; see note at
the end).

**What basegen actually does, and why it's fast:** a 13-cover is either
*irredundant* (every one of its classes has a private point — remove any
one and coverage breaks) or it has at least one *redundant* class (remove
it and the rest still cover everything). This split is exhaustive.
Basegen searches each half separately and unions the results:
- **Irredundant half** (`enumerate_irredundant13` → `dfs_irred`): a
  dedicated search that tracks, via `IrState`, whether every selected
  class still has a private point, pruning far harder than the general
  bound (this is where v6.2's exact slots==2 finish and v6.4's
  `need >= slots` check live — both *specific to* irredundant search).
- **Redundant half** (`enumerate_12covers` + `reducible_from_12`): rather
  than searching size-13 covers with a redundant class directly, basegen
  searches for complete covers using **at most 12 coordinate slots**
  (support size 1-12, with `compositions_rec` distributing repeats/
  multiplicities across a smaller support so the coordinate count is
  always exactly 12 regardless of support size), then extends each by
  **exactly one** more coordinate to reach 13. This is cheap specifically
  *because* the coordinate count is fixed at 12 — multiplicities absorb
  the gap between "how many distinct classes were used" and "how many
  coordinate slots that fills."

**Why the redundant half doesn't port as-is:** `find_cover.h`'s
`SpeedSet<K>` has **no multiplicity concept** — `Dfs::run` always picks
exactly `K` *distinct* classes (enforced by `AvailableChoice` elimination;
see `find_cover.h:79-94`). There is no "coordinate count" separate from
"number of distinct classes chosen." So a redundant `K`-cover here can
have a minimal complete sub-cover of *any* size `s` from 1 to `K-1` — it
might need `K-1` more picks padded on (if `s=1`), not always exactly 1
like basegen's fixed-at-12-coordinates trick guarantees. A literal port
(`enumerate (K-1)-covers, extend by +1`) would be **wrong** — it would
miss every redundant cover whose minimal sub-cover has fewer than `K-1`
distinct classes.

The natural generalization — search every support size `s = 1..K-1` for
complete covers, then combinatorially choose `K-s` *more* distinct classes
from what's left to pad each one out to exactly `K` — is not obviously a
net win: that padding step is a real `C(remaining, K-s)`-sized
combinatorial expansion, not O(1) like basegen's "+1 coordinate," and
could dominate cost for small `s`. Whether it's cheaper than today's
single general search is an open question that needs to be measured, not
assumed.

**Recommended path, in order of confidence:**
1. **Build the irredundant-only search first, as an addition, not a
   replacement.** Add a `find_all_irredundant_covers<P,K>()` alongside the
   existing `find_all_covers_parallel<P,K>()`, using `Dfs` extended with
   private-point tracking (mirroring `IrState`/`add_irred`, or the
   simpler `add_irred_logged`/`undo_irred` form if item 11's undo-log
   infra already exists). This part *does* transfer cleanly (irredundancy
   is defined purely in terms of the chosen distinct-class set, no
   multiplicity machinery involved) and should give a real, measurable
   speedup for the irredundant subset on its own — validate this in
   isolation (compare against filtering the existing full output for
   irredundant covers) before touching anything else.
2. **Only then** decide what to do about the redundant half: either (a)
   keep using the general search (now cheaper thanks to items 2-11 and
   able to skip re-deriving irredundant covers if the irredundant pass's
   results are subtracted/excluded via some cheap marker), or (b)
   prototype the "search every support size `s`, pad combinatorially"
   generalization above and *measure* whether it beats (a) — don't assume
   it will just because basegen's version wins there; basegen's win
   specifically depends on the multiplicity trick this codebase doesn't
   have.
3. Given the uncertainty in step 2, and that step 1 alone is already a
   nontrivial, independently-valuable change, treat this item as **two
   sub-items in practice** when it's actually implemented: land the
   irredundant-only search and its validation first, then treat the
   redundant-half question as its own follow-up decision (possibly "keep
   the current general search, it's fine") rather than a single monolithic
   change.

### 2. Necessary-condition short-circuit

**Origin:** v4/v5 (`avail.count() < slots`).
**Impact:** Small-Medium.
**Effort:** Trivial. **Depends on:** nothing.

**Correction (found during a prior implementation attempt of this item):**
basegen's `need >= slots` check (v6/"v6.4") does **not** port here. That
bound relies on **irredundancy** — in basegen's `dfs_irred`, every
remaining class must end with a private point, so fewer uncovered points
than remaining slots is a contradiction. `find_cover.h`'s *general* search
(`find_all_covers_parallel`/`Dfs::run`, as it exists today) has no such
requirement: its only leaf condition is `state.covered.count() == bitlen`
at exactly `K` elements (`find_cover.h:67-71`), and when coverage
completes early, `run()`'s loop condition `nextToCover == -1 || ...`
(`find_cover.h:82`) deliberately lets *any* remaining available class pad
out the rest of the `K` picks — redundant (fully-overlapping) picks are
legal, currently-working completions here. Applying `need >= slots` to
this general search would silently drop valid solutions. **Only the
`avail.count() < slots` half is ported** — that one holds regardless of
irredundancy (you always need `slots` more *distinct* classes, full
stop). (If/when item 1's dedicated irredundant search is built, *that*
search — being genuinely irredundancy-constrained — is exactly where
`need >= slots` would legitimately apply, same as it does in basegen's
`dfs_irred`.)

Add, before the existing (or new, item 7) bound computation — cheapest as
an unconditional check inside `early_return_bound`, ahead of the
`elems.size() >= K - 4` gate, since it's O(1) and should run every node:
`if (state.choice.availableCount() < K - state.elems.size()) return true;`
(new `availableCount()` accessor on `AvailableChoice`, backed by an
incrementally-maintained counter — see implementation below).

### 3. Custom `Bits` type + represent class-availability as a bitset (not a char array)

**Origin:** v4/v5's `Bits` type and its `avail` parameter, which is always
a `Bits`, never a per-index array.
**Impact:** Enabler (broad multiplier) — nothing below is cheap without it.
**Effort:** Medium. **Depends on:** nothing.

`find_cover.h` today has two different representations for two different
bit-vector roles, neither of which exposes raw words:
- `CoveredBitset = std::bitset<P/2>` for *covered points* — fine as a type,
  but `std::bitset` (at least libc++, which is what `clang++` on macOS
  uses per `run.sh`) does not expose word-level iteration.
- `AvailableChoice::_eliminated` / `_remaining` — `std::array<char, P/2>`
  for *class availability* — not a bitset at all, so every "is class i
  still available" check anywhere costs a full linear scan to enumerate.

Add a small fixed-word bitset, mirroring basegen's `Bits`, sized for
whichever dimension needs it (`P/2` bits for both points and classes here,
since `bitlen == P/2 == n`):

```cpp
template <int N> struct Bits
{
  static constexpr int NW = (N + 63) / 64;
  uint64_t w[NW] = {};

  void set(int i) { w[i >> 6] |= 1ULL << (i & 63); }
  void reset(int i) { w[i >> 6] &= ~(1ULL << (i & 63)); }
  bool test(int i) const { return (w[i >> 6] >> (i & 63)) & 1; }
  int count() const { int s = 0; for (int j = 0; j < NW; ++j) s += __builtin_popcountll(w[j]); return s; }
  bool any() const { for (int j = 0; j < NW; ++j) if (w[j]) return true; return false; }
};
template <int N> Bits<N> operator|(Bits<N> a, const Bits<N>& b) { for (int j=0;j<Bits<N>::NW;++j) a.w[j]|=b.w[j]; return a; }
template <int N> Bits<N> operator&(Bits<N> a, const Bits<N>& b) { for (int j=0;j<Bits<N>::NW;++j) a.w[j]&=b.w[j]; return a; }
template <int N> Bits<N> operator~(Bits<N> a) { for (int j=0;j<Bits<N>::NW;++j) a.w[j]=~a.w[j]; return a; }
```

Use `Bits<P/2>` in place of `CoveredBitset` (drop-in — same semantics as
`std::bitset` for `set`/`test`/`count`/`&`/`|`/`~`), and add a second
`Bits<P/2> avail` field to `AvailableChoice` that gets a bit `reset` when a
class is eliminated, replacing `_eliminated`. Keep `_remaining` (see item
5's note — it's not a basegen idea, but it's already a legitimate
optimization find_cover.h has that basegen doesn't, so don't remove it).

### 4. Transposed per-point candidate table `cand[point]`

**Origin:** v4/v5's `cand` vector (present since v4).
**Impact:** Large (enabler for items 6, 10).
**Effort:** Low-Medium. **Depends on:** item 3 (for the `Bits` type).

`Context` currently only stores `cov[class] -> Bits of points`. Add the
transpose:

```cpp
template <int P, int K> struct Context
{
  using CoveredBitset = Bits<P / 2>;
  ...
  std::array<CoveredBitset, P / 2> mCand{}; // mCand[point] = classes covering it

  Context()
  {
    for (int i = 0; i < P / 2; ++i)
      for (int t = 1; t <= P / 2; ++t)
      {
        int pos = P / 2 - t;
        int rem = (1LL * t * (i + 1)) % P;
        if ((rem * (K + 1) < P) || ((P - rem) * (K + 1) < P))
        {
          mCover[i].set(pos);
          mCand[pos].set(i);
        }
      }
  }
  const CoveredBitset& cand(int pos) const { return mCand[pos]; }
};
```

This is what turns "find every class that covers point `t`" from an O(P/2)
scan into a single stored `Bits`, ready to intersect with `avail`.

### 5. Word-scan bit iteration

**Origin:** v6.1.
**Impact:** Medium-Large (constant-factor multiplier across every hot loop).
**Effort:** Low-Medium. **Depends on:** item 3 (needs raw `w[]` access).

Add a helper and use it everywhere a `Bits` needs its set bits enumerated:

```cpp
template <int N, class F> void for_each_set_bit(const Bits<N>& b, F&& f)
{
  for (int j = 0; j < Bits<N>::NW; ++j)
  {
    uint64_t w = b.w[j];
    while (w) { int i = 64 * j + __builtin_ctzll(w); w &= w - 1; f(i); }
  }
}
```

Apply it to:
- `AvailableChoice::get_next_to_cover` (`find_cover.h:216-226`): currently
  tests every position `0..bitlen`. Word-scan the *uncovered* positions
  instead (`~current_covered`), and — matching basegen's
  `rarest_uncovered` early exit — `break`/return as soon as a position
  with `_remaining[pos] <= 1` is found (can't do better than 1).
- `AvailableChoice`'s constructor and `eliminate()` (`find_cover.h:205-233`):
  both loop `for (int pos = 0; pos < bitlen; ++pos) if (context.cover(i)[pos]) ...`
  — word-scan `context.cover(i)`'s set bits instead of testing every
  position.
- The candidate/gain loops introduced in items 6 and 7.

### 6. Restrict candidate enumeration to `cand[bt] & avail`

**Origin:** v4/v5 (`Bits choices=cand[bt]&avail; for v in 0..n if choices.test(v)`).
**Impact:** Large.
**Effort:** Low once items 3-4 land.

`Dfs::run`'s main loop (`find_cover.h:79-94`) currently scans **all**
`P/2` classes every node:

```cpp
for (int i = 0; i < P / 2; ++i)
{
  if (state.choice.isEliminated(i)) continue;
  if (nextToCover == -1 || context<P, K>.cover(i)[nextToCover]) { ... }
}
```

Replace with an intersection against the new `cand` table:

```cpp
auto choices = (nextToCover == -1) ? state.choice.avail
                                    : (context<P, K>.cand(nextToCover) & state.choice.avail);
for_each_set_bit(choices, [&](int i) { ... });   // see item 5 for for_each_set_bit
```

Same fix applies to `early_return_bound`'s `bestCovering_next` computation
(`find_cover.h:111-119`): instead of scanning all `P/2` classes and
filtering by `context<P,K>.cover(i)[nextToCover]`, iterate
`context<P,K>.cand(nextToCover) & state.choice.avail` directly. (The
`bestCovering` half of that loop still needs to scan all of `avail`,
since it's a bound over *every* remaining class, not just those covering
one point — but with `avail` now a `Bits`, that scan is also word-scannable,
see item 5.)

### 7. Tighten `early_return_bound` into a real `gain_bound`

**Origin:** v4/v5 (`gain_bound` itself: sum of top-`slots` gains via
`nth_element`, applied at every node) + v6.2 (O(1) `need > slots*m`
short-circuit, fixed top-k buffer for `slots <= 4`, total-sum shortcut,
`bsum` output for reuse by item 8).
**Impact:** Huge — this is basegen's single biggest documented win (~3x).
**Effort:** Medium-High. **Depends on:** items 3 and 5 (`Bits avail` +
word-scan iteration over it — this bound scans *all* of `avail`, not the
`cand`-restricted subset, so it doesn't need items 4/6). The pure
bound-tightening logic itself can technically be written before those
land, but it'll be slow until they do.

Current `early_return_bound` (`find_cover.h:100-123`) has two weaknesses
versus basegen's `gain_bound`:
1. It only runs once `state.elems.size() >= K - 4` (an explicit
   "TODO: arbitrary" gate) — basegen's bound runs at **every** node.
2. It approximates the remaining `slots-1` classes' combined gain as
   `bestCovering * (slots - 1)` — reusing one value repeated — instead of
   the actual sum of the top-`slots` *distinct* gains, which is strictly
   tighter (real top-k sum `<=` `k * max`).

Replace with something structurally equivalent to basegen's `gain_bound`:

```cpp
// m = fixed number of points every class covers (assert this at Context
// construction time — same invariant basegen relies on).
bool gain_bound(const CoveredBitset& unc, int need, const Bits<P/2>& avail,
                 int slots, int& bsum) const
{
  if (need > slots * m) return false;   // O(1) cardinality short-circuit (v6.2)

  if (slots <= 4)
  {
    int top[4] = {}, ng = 0;
    for_each_set_bit(avail, [&](int v) {
      ++ng;
      int g = (context<P,K>.cover(v) & unc).count();
      for (int k = 0; k < slots; ++k)
        if (g > top[k]) { for (int z = slots-1; z > k; --z) top[z] = top[z-1]; top[k] = g; break; }
    });
    if (ng < slots) return false;
    int sum = 0; for (int k = 0; k < slots; ++k) sum += top[k];
    bsum = sum - top[slots-1];
    return sum >= need;
  }

  // slots > 4: nth_element fallback with a total-sum shortcut (v6.2)
  static thread_local std::array<int, P/2> gains;
  int ng = 0; long long total = 0;
  for_each_set_bit(avail, [&](int v) {
    int g = (context<P,K>.cover(v) & unc).count();
    gains[ng++] = g; total += g;
  });
  if (ng < slots || total < need) return false;
  std::nth_element(gains.begin(), gains.begin()+slots, gains.begin()+ng, std::greater<int>());
  long long sum = 0; int mn = INT_MAX;
  for (int i = 0; i < slots; ++i) { sum += gains[i]; mn = std::min(mn, gains[i]); }
  bsum = (int)(sum - mn);
  return sum >= need;
}
```

Call it unconditionally at the top of `Dfs::run` (after the `elems.size()
== K` leaf check), not gated by `K-4`. This also folds in the
`bestCovering_next`-style "mandatory point" tightening basegen does via
`gain_bound2`/`gain_bound3` for small `slots` if you want the extra edge
— those are optional refinements on top of the generic bound above and
can be added later without changing the interface (`gain_bound` already
returns `bsum` either way).

### 8. Gain-sorted children, computed once per node

**Origin:** v4/v5 (sorts children by gain at all — currently missing
entirely from `find_cover.h`) + v6.1 (hoists the gain computation out of
the comparator).
**Impact:** Medium standalone; **large as an enabler** for item 9.
**Effort:** Low. **Depends on:** items 5-6.

`find_cover.h`'s `Dfs::run` loop currently visits candidates in raw
class-index order — no gain ordering at all. Add it:

```cpp
struct ScoredChild { int gain, v; };
std::array<ScoredChild, P/2> buf;
int nch = 0;
for_each_set_bit(choices, [&](int v) {
  buf[nch++] = { (context<P,K>.cover(v) & unc).count(), v };
});
std::sort(buf.begin(), buf.begin() + nch, [](auto&a, auto&b){
  return a.gain != b.gain ? a.gain > b.gain : a.v < b.v;
});
```

computed once into `buf`, not inside the comparator (matches v6.1, not
just v4/v5's original weaker version).

### 9. Child-gain cutoff

**Origin:** v6.3.
**Impact:** Large (basegen measured -37% nodes).
**Effort:** Low. **Depends on:** items 7 and 8 (needs the tightened
bound's `bsum` and gain-sorted children).

In the (now gain-sorted, per item 8) child loop, break as soon as a
child's own gain plus the bound on the rest can't reach `need`:

```cpp
for (int ci = 0; ci < nch; ++ci)
{
  if (buf[ci].gain + bsum < need) break;   // children are gain-sorted -> rest are dead too
  int v = buf[ci].v;
  ... // existing insert/recurse/remove logic
}
```

### 10. Exact closed-form finish at the last two picks

**Origin:** v6.2.
**Impact:** Large (basegen's single biggest measured win, 373s -> 125s).
**Effort:** Medium-High. **Depends on:** items 4-5 (cand table, word-scan)
and item 7 (a slots==2-shaped bound to fast-reject before doing the
intersection work). Does **not** depend on item 11 (undo-log) — they're
independent, and this item has strictly larger impact, hence the order.

When exactly 2 classes remain to pick (`K - state.elems.size() == 2`),
don't branch generically. After choosing the first of the two, the set of
valid second classes is exactly the intersection of `cand(t)` over every
still-uncovered `t`:

```cpp
if (K - state.elems.size() == 2)
{
  // as in basegen's dfs_irred slots==2 branch: pick first class via the
  // normal gain-sorted/cutoff loop, then for each candidate first pick,
  // intersect cand(t) over all remaining-uncovered t directly instead of
  // recursing into a third level of branching.
  ...
  return;
}
```

This is one of the most invasive items on the list — it's a genuinely new
code path, not a tweak to the existing loop — so land it after the core
bound/ordering machinery (2-9) is in place and validated (you want a
solid bound to fast-reject first picks before paying for the intersection
on each). It's ordered *before* item 11 despite that, because its impact
is larger and nothing about it depends on item 11.

### 11. Undo-log instead of full per-node state copy

**Origin:** v6.1.
**Impact:** Medium.
**Effort:** Medium. **Depends on:** item 3 (do this after the
`AvailableChoice` representation is settled, so you're not rewriting the
undo logic twice). Nothing later depends on this item, which is why it's
last despite being no more "invasive" than item 10 — it simply has the
smallest payoff of the remaining work.

`Dfs::run` currently does a full-struct save/restore of `AvailableChoice`
once per node (`find_cover.h:76,96`: `const auto saved_choice = state.choice;`
/ `state.choice = saved_choice;`) plus a full `CoveredBitset` copy per
child attempt (`find_cover.h:85,92`: `CoveredBitset mem = state.covered;`
/ `state.covered = mem;`). With `avail` now a `Bits<P/2>` (item 3) and
`_remaining` still a small array, mutate-and-log instead of copying: when
eliminating class `i`, record only the `_remaining[pos]` entries actually
decremented (bounded by `m`, the fixed per-class coverage count) in a
small fixed buffer, then replay in reverse to undo — same shape as
basegen's `add_irred_logged`/`undo_irred`. The `CoveredBitset` restore is
already cheap (a few `uint64_t` words once item 3 lands) and doesn't need
this treatment — focus the undo-log on `AvailableChoice`.

---

## Suggested implementation order

**1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10 → 11** — the numbering above *is*
the implementation order now. In words:

1. Irredundant/reducible search decomposition — placed first by choice
   (independent of 2-11; see Part 2's opening note). Treat the
   irredundant-only sub-search as the confident part to build and
   validate first; the redundant-half strategy is an open design question
   to resolve as a follow-up, not assumed up front.
2. Necessary-condition short-circuit — zero-dependency, cheap.
3. Custom `Bits` type + `avail` bitset — foundational, unlocks everything else.
4. Transposed `cand[point]` table — depends on 3.
5. Word-scan bit iteration — depends on 3.
6. Restrict candidate enumeration to `cand[bt] & avail` — depends on 3-4.
7. Tightened `gain_bound` — depends on 3 and 5; highest-impact single item among 2-11, land as soon as its dependencies allow rather than waiting for 6/8.
8. Gain-sorted children (hoisted) — depends on 5-6; exists to enable 9.
9. Child-gain cutoff — depends on 7-8.
10. Exact slots==2 closed-form finish — depends on 4-5 (+7); larger impact than 11, ordered before it.
11. Undo-log — depends on 3 only; last because nothing depends on it and it has the smallest remaining payoff.

Validate each step the way basegen's own commits did: build before/after,
diff full output (`SetOfSpeedSets<K>` contents) on a couple of small
`(P,K)` instances, confirm byte-identical results, *then* look at timing.

---

## Per-item workflow

Each numbered item above is implemented **one at a time**, only on
explicit request naming that item. For the requested item:

1. **Read this file in full** first if it hasn't already been read this
   session.
2. **Implement** exactly the section requested — no more (don't start the
   next item), no less (don't leave it partial).
3. **Verify statically**: re-read the diff against this file's own
   description/pseudocode for that item; confirm it matches the stated
   intent and doesn't silently change behavior the item doesn't call for.
4. **Compile and run `test.cpp`** (repo root) against the change — this
   file is the fixed, permanent oracle: it computes `sha256` of a
   deterministically-sorted text dump of `find_all_covers_parallel<P,K>()`'s
   output for 8 fixed `(K,P)` cases, entirely self-contained (embeds its
   own SHA-256, no external tools). **`test.cpp` itself must never be
   edited again** — it was committed once as ground truth. When verifying
   an alternative/decomposed implementation meant to be a drop-in
   replacement for `find_all_covers_parallel` (e.g. item 1's
   `find_all_irredundant_covers` ∪ `find_all_redundant_covers`), copy
   `test.cpp`'s `run_case` body into a scratch file, swap in the
   replacement computation, and confirm every printed `sha256` matches the
   values below exactly — do not hand-edit or re-derive the reference
   values themselves.

   Fixed reference values (`p = 199` appears for every `K` deliberately —
   basegen's own long-standing benchmark/instrumentation prime, per the
   `p==199` special cases throughout `basegen_v5.cpp`/`basegen_v6.cpp`;
   `461`/`211` were chosen by probing `LrcVerifier<K>`'s prime list to land
   close to ~2 minutes on the unoptimized baseline — see
   `improvement_implementation.md` for the probe data):

   | K | P | count | sha256 |
   |---|---|---|---|
   | 10 | 127 | 8228 | `9ea3c09622858900e02f397a7413663ee6f67c37c8cd365d6cc7f4f9249e994c` |
   | 10 | 199 | 4417 | `10964f848c825ad6d52903c8d7c6056c3aa5afe241d1411d718e3831053ff7e8` |
   | 10 | 461 | 1 | `d309dd5f6192e467dd90935144f4eac4f89445ea2b019f65d9437e877ba88c1e` |
   | 11 | 131 | 40615 | `b5b3ec828226e19c517afbd659aa5a399c32ee6f14cbb459b8e2228aa8376673` |
   | 11 | 199 | 18516 | `3e7c5ffc251b4f330c4f87f66a0e7b562f431f345cd7a6124ec62b4086d78679` |
   | 12 | 139 | 641960 | `37da0b2667c68035669af9dd15bb788812a42632b4e07c2435b2c0b8fbf6fffe` |
   | 12 | 199 | 494183 | `da84151e5b6998af2d6ae3147cf04d51ed173f5cf25e7f6fc322cd07b0799ea4` |
   | 12 | 211 | 426537 | `4db6f3c04be25e9893bcc5e1e8c8dabdd69fbc3d1db155d3d0b25b19d3fa3c6a` |

   Generated from a clean checkout at commit `57ff576` (no
   `improvement_plan.md` optimization items applied). Timeout: 180s/case;
   if a run needs longer, that's a signal to pick a smaller prime for
   routine validation, not to silently raise the timeout.

5. **If outputs mismatch**: the change is wrong — debug and fix before
   doing anything else in steps 6+. Do not proceed on a known mismatch.
6. **If outputs match**: append an entry to `improvement_implementation.md`
   (create it on the first item) recording — which item, a summary of the
   diff, confirmation of the byte-identical regression results, and timing
   before/after for each test case.
7. **Ask the user to review and approve** that new entry.
8. **Once approved, commit** (the code change and the
   `improvement_implementation.md` update together).
9. **Stop and wait** for the user's next prompt before starting another
   item — do not chain into the next numbered item on your own.
