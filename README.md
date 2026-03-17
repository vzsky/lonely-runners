# Lonely Runners Conjecture

Work in progress.

See [here](https://github.com/t-tanupat/nine-and-ten-lonely-runners) for the original code.

> **If the conjecture holds for 11 and 12 runners.**

## Accompanying files: 
- `main.cpp` the source code 
- `run.sh` just the compilation guide
- `results/result_{k+1}_{description}` result of running the source
    - for `k=11` it's a combination of many runs
- `log_verifier.py` read `result_k` and verify the proof (size of primes).
- `happy_check.py` read `result_11` and check if all seeds match the happy condition.
    - this will not complete the proof on its own. need the happy checker for each prime.
- `article/` the source of the article.
