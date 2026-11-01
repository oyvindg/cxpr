# IR optimization iteration 03: typed lowered IR

Iteration: 3

Hypothesis: replacing the int64-classified typed `cxpr_value` interpreter with closed
int64/bool stacks removes construction and per-opcode type dispatch while retaining exact
overflow and comparison semantics.

Changed:

- `src/ir/lower.c`, `src/ir/internal.h`: immutable executable value-kind metadata
  (`none`, `dynamic`, `f64`, `i64`, `bool`) for every instruction result.
- `src/ir/exec/int64.c`: closed int64/bool executor with checked arithmetic, exact comparisons,
  boolean control flow, and explicit typed fallback for unsupported dynamic programs.
- `benchmarks/int64_ir_bench.c`: repeated public-API benchmark with parse/compile outside timing.

Benchmark commands:

```sh
cc -O3 -DNDEBUG -Iinclude benchmarks/int64_ir_bench.c build/libcxpr.a -lm \
  -o /tmp/cxpr_phase2_exact_int64_baseline
for i in 1 2 3 4 5 6 7 8 9; do
  /tmp/cxpr_phase2_exact_int64_baseline
  build/benchmarks/cxpr_bench_ir_int64
done
```

Environment: baseline `01e35ff`; GCC 13.3.0; Release `-O3 -DNDEBUG`; AMD Ryzen 7 5800H,
16 logical CPUs. CPU frequency was not pinned. `perf` was unavailable.

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Throughput delta | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| closed int64 public eval | 17.00 ns | 11.63 ns | -5.37 ns | -31.59% | 1.462x | +46.17% | keep |

P95 improved from 18.184 to 11.690 ns (-35.71%). A paired engine smoke retained parity and
showed raw-program paths effectively flat (basic 431.15 -> 430.70 ns/bar; lookback
296.36 -> 301.79 ns/bar) and engine watch slightly improved (basic 234.06 -> 227.29;
lookback 435.90 -> 432.96). Model smoke stayed within low-single-digit run variance.

Correctness:

- Release: 138/138 tests passed; `bulk_reduce` remains the documented skip.
- ASan: 5/5 focused int64/IR/program tests passed with leak detection disabled due container
  ptrace restrictions.
- Tests cover exact values above 2^53, checked addition overflow, int64 arithmetic,
  bool comparisons, stack separation, and AST/IR error parity.
- IR dumps expose per-instruction lowered kinds; `dynamic` marks typed fallback boundaries.

Assessment: keep. Scalar and boolean fast executors already used split primitive stacks;
int64-classified compatible programs now do the same. Programs containing dynamic calls,
mixed numeric values, typed context lookup, or unsupported opcodes retain the general typed
executor rather than weakening semantics.

Next hypothesis: resolve producer result schemas and lower known field reads directly to typed
result slots (Phase 3), avoiding struct allocation and field-name scans.

