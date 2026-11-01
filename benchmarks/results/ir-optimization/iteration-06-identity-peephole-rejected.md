# IR optimization iteration 06

Iteration: 6

Hypothesis: Removing IEEE-safe right identities (`x * 1`, `x / 1`, and `x - +0`) in a post-validation peephole pass will reduce representative IR instruction counts and runtime.

Changed: Exploratory `src/ir/optimize.c` pass plus focused tests; all implementation changes were removed after the gate failed.

Benchmark command: `build/benchmarks/cxpr_bench_ir --case large_arith`, `--case large_branch`, `--case defined_call`, and focused identity fixtures.

Environment: base `11f7cca`; GCC 13.3.0; Release; AMD Ryzen 7 5800H.

Before: `large_arith` 113 instructions; `large_branch` 124 instructions; `defined_call` 11 instructions.

After: `large_arith` 113 instructions; `large_branch` 124 instructions; `defined_call` 11 instructions. Synthetic identity expressions decreased from four to two instructions.

Delta: zero instructions and no measurable representative runtime improvement. Synthetic-only reduction is insufficient under the benchmark gate.

Correctness: Focused identity, signed-zero, and control-flow tests passed before rejection.

Assessment: reject. The representative fixtures contain no matching identity sequence, so keeping this pass would add maintenance and compile work without demonstrated consumer value.

Next hypothesis: Deduplicate repeated pure loads/subexpressions using explicit evaluator-owned temporary slots, targeting measured repetitions in `large_arith` while preserving control-flow semantics.

| Benchmark | Before instructions | After instructions | Delta | Decision |
| --- | ---: | ---: | ---: | --- |
| `large_arith` | 113 | 113 | 0 | reject |
| `large_branch` | 124 | 124 | 0 | reject |
| `defined_call` | 11 | 11 | 0 | reject |
