# IR optimization iteration 02: resolved input slots

Iteration: 2

Hypothesis: resolving each unique scalar input once per evaluator/session and reading guarded
direct pointers in the hot executor removes repeated lookup validation without putting mutable
state in a shared compiled program.

Changed:

- `src/ir/bindings.c`, `src/ir/internal.h`: immutable deduplicated input descriptors,
  generation identities, and evaluator/session-owned bindings.
- `src/ir/exec/`: bound scalar/bool/typed execution, guarded direct reads, cached fallback for
  overlays/dynamic scopes, and a 16-entry TLS cache for standalone compiled programs.
- `src/expression/`, `src/model/`: bindings owned by evaluator entries and model sessions;
  fused sessions retain their existing pre-bound-local path.

Benchmark commands:

```sh
for i in 1 2 3 4 5 6 7 8 9; do build/benchmarks/cxpr_bench_engine; done
for i in 1 2 3 4 5 6 7 8 9; do build/benchmarks/cxpr_bench_model; done
build/benchmarks/cxpr_bench_ir --case simple_arith
build/benchmarks/cxpr_bench_ir --case large_arith
build/benchmarks/cxpr_bench_ir --case context_churn
build/benchmarks/cxpr_bench_index_history
build/benchmarks/cxpr_bench_resample
```

Environment: baseline commit `4a0852b`; GCC 13.3.0; Release `-O3 -DNDEBUG`;
AMD Ryzen 7 5800H, 16 logical CPUs. CPU frequency was not pinned and the scalar microbenchmarks
were visibly bimodal, so the stable engine/model cases are the decision workloads. `perf` was
not installed; hardware counters are unavailable.

Before/after values are median / p95 ns per bar or operation. Throughput deltas are computed
from median latency.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Throughput delta | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| basic evaluator | 595.90 | 450.98 | -144.92 | -24.32% | 1.321x | +32.13% | keep |
| basic engine watch | 363.62 | 220.52 | -143.10 | -39.35% | 1.649x | +64.89% | keep |
| lookback evaluator | 444.38 | 357.11 | -87.27 | -19.64% | 1.244x | +24.44% | keep |
| lookback engine watch | 501.44 | 427.27 | -74.17 | -14.79% | 1.174x | +17.36% | keep |
| strategy fixture | 5021.27 | 4956.50 | -64.77 | -1.29% | 1.013x | +1.31% | keep |
| RSI fused IR | 714.02 | 720.98 | +6.96 | +0.97% | 0.990x | -0.97% | neutral |
| MACD record | 1986.46 | 2031.80 | +45.34 | +2.28% | 0.978x | -2.23% | accepted |

P95 changed as follows: basic evaluator 609.48 -> 454.49 (-25.43%), basic engine
368.41 -> 223.50 (-39.33%), lookback evaluator 463.77 -> 358.94 (-22.60%), lookback engine
533.77 -> 434.49 (-18.60%), strategy 5109.50 -> 5061.56 (-0.94%), RSI 737.29 -> 736.10
(-0.16%), and MACD 2043.27 -> 2060.61 (+0.85%). The two small model median regressions are
below 3%, have flat p95, and are outside the optimized input-bound consumer path.

Correctness:

- Release: 138/138 tests passed; `bulk_reduce` remains the documented skip.
- ASan: 7/7 focused IR/expression/model tests passed with leak detection disabled because
  LeakSanitizer cannot operate under the container ptrace restrictions.
- Added descriptor deduplication, structural context-growth rebinding, and concurrent shared
  compiled-program tests (two threads, independent contexts, 100,000 evaluations each).
- Engine benchmark parity: OK in every measured trial.
- Index/history and resample benchmark smoke runs completed without parity or runtime failure.

Assessment: keep. Compiled programs are immutable and shareable; mutable caches live in the
evaluator/session or thread-local standalone cache. Structural map changes cause a rebind,
ordinary value writes continue through stable pointers, and expression-scope names use the
existing typed/cached fallback.

Next hypothesis: remove typed `cxpr_value` traffic for statically known scalar/bool/int64
execution (Phase 2), while preserving the bound input table introduced here.

