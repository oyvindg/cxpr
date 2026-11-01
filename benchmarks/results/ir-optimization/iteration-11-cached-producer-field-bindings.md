# IR optimization iteration 11

Iteration: 11

Hypothesis: Binding constant producer fields to evaluator-owned cached struct values removes three context-map lookups per evaluation while an explicit cache generation preserves invalidation semantics.

Changed: `src/context/state.h`, `src/context/lifecycle.c`, and `src/context/structs.c` add a cached-struct generation; `src/ir/bindings.c`, `src/ir/internal.h`, and `src/ir/exec/typed.c` resolve and consume guarded producer-field pointers; `tests/eval_calls.test.c` verifies reuse and rebinding after cache clear.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_ir --case producer_fields` at before/after revisions.

Environment: before `f78c943`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: `producer_fields` median 45.94 ns/eval, p95 47.36 ns, 21.768 M eval/s.

Profit previous: `producer_fields` median 141.74 ns/eval, p95 193.88 ns, 7.055 M eval/s.

Delta profit: -95.80 ns (-67.59%, 3.085x speedup); throughput +14.713 M eval/s (+208.5%).

Other key metrics: Six IR instructions and zero AST fallbacks before and after. The registered producer remains invoked once while its three cached fields are reused.

Correctness: 138/138 Release tests pass (one documented skip). Focused ASan/UBSan passes for `eval_calls`, `ir_exec_typed`, and `context_lifecycle` with leak detection disabled due to host ptrace policy. The regression test clears the producer cache between compiled evaluations and verifies that the producer is invoked again.

Assessment: keep. Cached pointers remain evaluator-owned and are invalidated by context identity, map generations, or the dedicated cached-struct generation.

Next hypothesis: Reassess remaining typed dispatch overhead; do not start profile-gated superinstructions without a concrete bottlenecked consumer.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `producer_fields` | 141.74 ns | 45.94 ns | -95.80 ns | -67.59% | 3.085x | keep |
