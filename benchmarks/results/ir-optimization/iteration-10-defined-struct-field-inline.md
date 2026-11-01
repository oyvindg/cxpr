# IR optimization iteration 10

Iteration: 10

Hypothesis: Compiling a selected field from a defined struct-returning function directly through its parameter substitution removes the temporary struct, defined-call overlay, and runtime field extraction.

Changed: `src/ir/compile/node.c` lowers safe selected return fields directly; `src/ir/bindings.c`, `src/ir/internal.h`, `src/ir/exec/fast.c`, and `src/ir/exec/typed.c` bind flat named scalar inputs directly; `benchmarks/ir_bench.c` verifies that these cases contain no `CALL_DEFINED`, `GET_FIELD`, or AST fallback.

Benchmark command: Nine independent runs of `build/benchmarks/cxpr_bench_ir --case <case>` at before/after revisions.

Environment: before `7202f32`; GCC 13.3.0; Release; AMD Ryzen 7 5800H. CPU governor unchanged; `perf` unavailable.

Profit now: `struct_call` median 36.59 ns/eval; `struct_binding` median 15.88 ns/eval.

Profit previous: `struct_call` median 834.27 ns/eval; `struct_binding` median 1310.11 ns/eval.

Delta profit: `struct_call` -797.68 ns (-95.61%, 22.80x speedup); `struct_binding` -1294.23 ns (-98.79%, 82.50x speedup).

Other key metrics: `struct_call` p95 838.74 -> 40.18 ns; `struct_binding` p95 1393.78 -> 32.68 ns. Instruction count is 5 -> 4 for `struct_call` and remains 4 for `struct_binding`, while both eliminate the expensive defined-call/temporary-struct path.

Correctness: 138/138 Release tests pass (one documented skip). Focused ASan/UBSan passes for `eval_calls`, `integration_flows`, `ir_compile`, and `program` with leak detection disabled due to host ptrace policy. A broader numeric-fast-path attempt exposed an invalid numeric-condition acceptance in the full suite and was rolled back; it is not part of this result.

Assessment: keep. Direct lowering is restricted to statically selected fields and identifier arguments for struct parameters; dynamic/record arguments retain the existing fallback semantics.

Next hypothesis: Profile the remaining named-field load and dispatch overhead before introducing any register IR or superinstruction complexity.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `struct_call` | 834.27 ns | 36.59 ns | -797.68 ns | -95.61% | 22.80x | keep |
| `struct_binding` | 1310.11 ns | 15.88 ns | -1294.23 ns | -98.79% | 82.50x | keep |
