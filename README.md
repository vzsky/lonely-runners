# The Lonely Runner Conjecture code

LRC holds for 13 runners. 
> each in a group of up to 13 runners will eventually be lonely

[preprint](https://arxiv.org/abs/2604.23906)

For speed, extensibility, and all the nice thing, we suggest checking out the `latest` branch.

## branches

- `main` contains the polished code, we checked that this proves k <= 10
- `for-k-12` is the earlier version of code. we ran this for k=11 and k=12
- `Allikvere` is the changes inspired by the Allikvere's implementation. See [fourteen-lonely-runner](https://arxiv.org/abs/2609.02604)
- `latest` is whatever the "recommended" reference I suggest you look at. 

## Accompanying files:

- `run.sh` just a compilation guide
- `results/*` result of running the source
- `log_summary.py` read `result_k` and give summary the proof.
  - this check for everything we know provided that the log files are correctly generated.
  - this is entirely AI generated... but it is fine as
  - this is for sanity check only
  - the problem this script solves is trivial enough
  - this is quite old and was incrementally added so that it work with all previous formatting that was once used.
- `article/` the source of the article.
- `freezer/` some regression tests.

## Implementation Details

We split this into small parts with three main components:

- `find_cover` handles finding $I(k, p, 1)$
  - `find_cover.h` for everything
- `lift` handles the lifting
  - `src/lift.h` for the lifting logic
  - `src/lift_strategy.h` for lifting strategy + utils
- `main` is the driver
  - `src/utils.h` for util functions
  - `src/driver.h` for logic dispatching all the calls
  - `main.cpp` main driver

## Remarks

- this is a lot faster than its ancestor when measured runtime wise.
- it takes half a minute to compile an optim binary.
  - one might argue it's still slow, we only shift the cost to compile time, for that i say:
    - well maybe, but it is _nicer_ this way.
    - technically everything here can be compile time, the art is where to stop
    - this is still faster measured with combined time.
    - this is fun
