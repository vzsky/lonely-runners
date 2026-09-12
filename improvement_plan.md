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
  provably dead code once this check exists.

### v6 proper (capacity widening — not portable, listed only for completeness)

- Wider `Bits` (up to 7 words), `uint16_t` class indices, and 64-bit quota
  masks (`qm`) for a `C1`-quota check specific to basegen's irredundant-13
  -cover enumeration. None of this applies to `find_cover.h`: it has no
  quota concept and its `SpeedSet<K>` / `P/2` sizes aren't hitting a
  capacity ceiling the way basegen's `n<=897` case was.

---

## Part 2 — Porting plan for `src/find_cover.h`

Ordered by the actual dependency graph (what genuinely blocks what),
using impact as the tiebreak wherever nothing forces a particular order.
Concretely:

- **#1** has zero dependencies and is nearly free — do it before anything else.
- **#2-#5** are infrastructure (`Bits` type, `cand` table, word-scan,
  restricted candidate iteration) that #6-#9 need in order to be fast;
  #2 must come before #3 (the table needs the bitset type), #4 needs #2,
  #5 needs #2+#3.
- **#6** (the tightened bound) only needs #2+#4 — it doesn't need children
  sorted, and it prunes whole subtrees regardless of child visit order —
  so it can land before #7 despite #7 being numerically "smaller."
- **#7** (gain-sorted children) exists specifically to enable **#8** (the
  cutoff); there's no reason to do it before #6, which delivers its (much
  larger) win independently.
- **#9** (exact slots==2 finish) only depends on #3+#4 (+ benefits from
  #6) — **not** on #10. It has *larger* impact than #10, so it's ordered
  before it, not after.
- **#10** (undo-log) depends only on #2, and nothing later depends on it.
  Since it's the lowest-impact item with no downstream dependents, it's
  correctly last — not because it's "invasive," but because nothing is
  gated on it and everything with a bigger payoff can land first.

Current relevant code (line numbers as of this writing):
- `Context` / `mCover`: `find_cover.h:23-46`
- `Dfs::State` / `run()`: `find_cover.h:53-97`
- `early_return_bound()`: `find_cover.h:100-123`
- `AvailableChoice`: `find_cover.h:196-234`

### 1. Necessary-condition short-circuits

**Origin:** v4/v5 (`avail.count() < slots`) + v6/"v6.4" (`need >= slots`).
**Impact:** Small-Medium.
**Effort:** Trivial. **Depends on:** nothing — these are guard clauses on
values `Dfs::run`/`early_return_bound` already compute (or can compute
with the current `char`-array `AvailableChoice`, no infra needed). Do
this first: it's a same-day change with no prerequisites.

Add, before the existing (or new, item 6) bound computation:
- `if (K - state.elems.size() > totalToCover) return;` — can't have more
  remaining slots than uncovered points needing a distinct contribution
  (mirrors basegen's `need >= slots`).
- `if (state.choice.remaining_avail_count() < K - state.elems.size()) return;`
  (or an O(n) count over `_eliminated` today, O(1) once item 2's `Bits
  avail` exists) — not enough candidate classes left to fill the
  remaining slots (mirrors basegen's `avail.count() < slots`).

### 2. Custom `Bits` type + represent class-availability as a bitset (not a char array)

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
4's note — it's not a basegen idea, but it's already a legitimate
optimization find_cover.h has that basegen doesn't, so don't remove it).

### 3. Transposed per-point candidate table `cand[point]`

**Origin:** v4/v5's `cand` vector (present since v4).
**Impact:** Large (enabler for items 5, 9).
**Effort:** Low-Medium. **Depends on:** item 2 (for the `Bits` type).

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

### 4. Word-scan bit iteration

**Origin:** v6.1.
**Impact:** Medium-Large (constant-factor multiplier across every hot loop).
**Effort:** Low-Medium. **Depends on:** item 2 (needs raw `w[]` access).

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
- The candidate/gain loops introduced in items 5 and 6.

### 5. Restrict candidate enumeration to `cand[bt] & avail`

**Origin:** v4/v5 (`Bits choices=cand[bt]&avail; for v in 0..n if choices.test(v)`).
**Impact:** Large.
**Effort:** Low once items 2-3 land.

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
for_each_set_bit(choices, [&](int i) { ... });   // see item 4 for for_each_set_bit
```

Same fix applies to `early_return_bound`'s `bestCovering_next` computation
(`find_cover.h:111-119`): instead of scanning all `P/2` classes and
filtering by `context<P,K>.cover(i)[nextToCover]`, iterate
`context<P,K>.cand(nextToCover) & state.choice.avail` directly. (The
`bestCovering` half of that loop still needs to scan all of `avail`,
since it's a bound over *every* remaining class, not just those covering
one point — but with `avail` now a `Bits`, that scan is also word-scannable,
see item 4.)

### 6. Tighten `early_return_bound` into a real `gain_bound`

**Origin:** v4/v5 (`gain_bound` itself: sum of top-`slots` gains via
`nth_element`, applied at every node) + v6.2 (O(1) `need > slots*m`
short-circuit, fixed top-k buffer for `slots <= 4`, total-sum shortcut,
`bsum` output for reuse by item 7).
**Impact:** Huge — this is basegen's single biggest documented win (~3x).
**Effort:** Medium-High. **Depends on:** items 2 and 4 (`Bits avail` +
word-scan iteration over it — this bound scans *all* of `avail`, not the
`cand`-restricted subset, so it doesn't need items 3/5). The pure
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

### 7. Gain-sorted children, computed once per node

**Origin:** v4/v5 (sorts children by gain at all — currently missing
entirely from `find_cover.h`) + v6.1 (hoists the gain computation out of
the comparator).
**Impact:** Medium standalone; **large as an enabler** for item 8.
**Effort:** Low. **Depends on:** items 4-5.

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

### 8. Child-gain cutoff

**Origin:** v6.3.
**Impact:** Large (basegen measured -37% nodes).
**Effort:** Low. **Depends on:** items 6 and 7 (needs the tightened
bound's `bsum` and gain-sorted children).

In the (now gain-sorted, per item 7) child loop, break as soon as a
child's own gain plus the bound on the rest can't reach `need`:

```cpp
for (int ci = 0; ci < nch; ++ci)
{
  if (buf[ci].gain + bsum < need) break;   // children are gain-sorted -> rest are dead too
  int v = buf[ci].v;
  ... // existing insert/recurse/remove logic
}
```

### 9. Exact closed-form finish at the last two picks

**Origin:** v6.2.
**Impact:** Large (basegen's single biggest measured win, 373s -> 125s).
**Effort:** Medium-High. **Depends on:** items 3-4 (cand table, word-scan)
and item 6 (a slots==2-shaped bound to fast-reject before doing the
intersection work). Does **not** depend on item 10 (undo-log) — they're
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

This is one of the two most invasive items on the list — it's a
genuinely new code path, not a tweak to the existing loop — so land it
after the core bound/ordering machinery (1-8) is in place and validated
(you want a solid bound to fast-reject first picks before paying for the
intersection on each). It's ordered *before* item 10 despite that,
because its impact is larger and nothing about it depends on item 10.

### 10. Undo-log instead of full per-node state copy

**Origin:** v6.1.
**Impact:** Medium.
**Effort:** Medium. **Depends on:** item 2 (do this after the
`AvailableChoice` representation is settled, so you're not rewriting the
undo logic twice). Nothing later depends on this item, which is why it's
last despite being no more "invasive" than item 9 — it simply has the
smallest payoff of the remaining work.

`Dfs::run` currently does a full-struct save/restore of `AvailableChoice`
once per node (`find_cover.h:76,96`: `const auto saved_choice = state.choice;`
/ `state.choice = saved_choice;`) plus a full `CoveredBitset` copy per
child attempt (`find_cover.h:85,92`: `CoveredBitset mem = state.covered;`
/ `state.covered = mem;`). With `avail` now a `Bits<P/2>` (item 2) and
`_remaining` still a small array, mutate-and-log instead of copying: when
eliminating class `i`, record only the `_remaining[pos]` entries actually
decremented (bounded by `m`, the fixed per-class coverage count) in a
small fixed buffer, then replay in reverse to undo — same shape as
basegen's `add_irred_logged`/`undo_irred`. The `CoveredBitset` restore is
already cheap (a few `uint64_t` words once item 2 lands) and doesn't need
this treatment — focus the undo-log on `AvailableChoice`.

---

## Suggested implementation order

**1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10** — the numbering above *is* the
implementation order now (that's what the resort in Part 2's intro
established). In words:

1. Necessary-condition short-circuits — zero-dependency, do first.
2. Custom `Bits` type + `avail` bitset — foundational, unlocks everything else.
3. Transposed `cand[point]` table — depends on 2.
4. Word-scan bit iteration — depends on 2.
5. Restrict candidate enumeration to `cand[bt] & avail` — depends on 2-3.
6. Tightened `gain_bound` — depends on 2 and 4; highest-impact single item, land as soon as its dependencies allow rather than waiting for 5/7.
7. Gain-sorted children (hoisted) — depends on 4-5; exists to enable 8.
8. Child-gain cutoff — depends on 6-7.
9. Exact slots==2 closed-form finish — depends on 3-4 (+6); larger impact than 10, ordered before it.
10. Undo-log — depends on 2 only; last because nothing depends on it and it has the smallest remaining payoff.

Validate each step the way basegen's own commits did: build before/after,
diff full output (`SetOfSpeedSets<K>` contents) on a couple of small
`(P,K)` instances, confirm byte-identical results, *then* look at timing.
