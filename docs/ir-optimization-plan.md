# CXPR IR Optimization Plan

## Status

- State: proposed
- Scope: `libs/cxpr` internal IR compilation and execution
- Public API impact: none intended
- Primary objective: reduce the runtime gap between interpreted IR and generated C

## Motivation

The 2026-09-13 performance baseline shows that generated C is substantially faster than
the current IR evaluator:

| Workload | IR | Generated C | IR/C gap |
| --- | ---: | ---: | ---: |
| Simple arithmetic | 23.39 ns | 2.98 ns | 7.85x |
| Large arithmetic | 201.42 ns | 7.06 ns | 28.52x |
| Complex signal | 71.07 ns | 5.60 ns | 12.69x |
| Lookback leaf | 140.52 ns | 2.82 ns | 49.81x |
| Struct/scalar, all fields | 1230.34 ns | 2.98 ns | 413.43x |
| Struct/struct add, all fields | 1915.25 ns | 3.15 ns | 608.38x |

The engine wrapper itself is a smaller cost: existing engine benchmarks show approximately
14-15% overhead over low-level evaluation. Optimization should therefore focus first on IR
lowering, data access, typed values, producer fields, and lookback execution.

Generated C/CUDA remains the production target when expressions are known at build time.
The optimized IR evaluator remains important for editor preview, diagnostics, tests,
dynamically supplied expressions, and environments where native compilation is unavailable.

## Goals

1. Reduce scalar IR execution to within 2-5x of generated C for representative expressions.
2. Remove the extreme typed-struct and lookback penalties.
3. Avoid name lookup, hashing, allocation, and dynamic type dispatch in steady-state scalar
   execution whenever operands can be resolved at compile or session creation time.
4. Preserve expression semantics, diagnostics, callback ordering, and public API behavior.
5. Keep generated C/CUDA and interpreted IR based on the same typed lowered representation.
6. Make every optimization independently benchmarkable and reversible.

## Non-goals

- Replacing generated C/CUDA with the interpreter.
- Adding LLVM, a machine-code JIT, or executable-memory management in this effort.
- Changing CXPR syntax or documented expression semantics.
- Exposing the internal bytecode or register representation as a stable serialization format.
- Optimizing parser and AST construction unless profiling proves they affect repeated runtime
  execution.

## Compatibility contract

The following public contracts must remain source- and behavior-compatible:

- expression parse, compile, and evaluation entry points;
- `cxpr_context`, registry, provider, and engine APIs;
- error codes and source locations;
- numeric, boolean, int64, NaN, division-by-zero, and overflow behavior;
- short-circuit evaluation and observable callback order;
- lookback and history semantics;
- generated C/CUDA output semantics.

`cxpr_expr_compiled` remains opaque. Its internal representation may change freely.

`include/cxpr/ir.h` exposes a logical inspection view, not serialized bytecode. Internal
instructions may be translated or expanded to the existing public v1 opcodes. Increase
`CXPR_IR_VIEW_API_VERSION` only if the public inspection contract itself must change.

Mutable binding and lookup state belongs to an evaluator or engine session. A compiled
program should remain shareable across threads unless an existing API explicitly documents
otherwise.

## Measurement protocol

Performance work must be developed as a benchmark-driven loop. A phase is not implemented in
one large pass and benchmarked afterward. Each logical optimization is a separate iteration
with at most one to three related changes.

### Iteration loop

For every optimization iteration:

1. Select one measured bottleneck and state the hypothesis.
2. Run the relevant benchmark suite and save the baseline result.
3. Apply one to three small, reversible changes.
4. Run correctness, parity, and sanitizer tests relevant to the changed path.
5. Run the same benchmark command, configuration, dataset, and trial count.
6. Calculate absolute and percentage deltas for median, p95, throughput, and any relevant
   hardware counters.
7. Keep the change only when the representative workload improves without an unacceptable
   regression elsewhere.
8. Revert the iteration when the hypothesis fails, then record the result so the same path is
   not repeated without new evidence.

Use these formulas consistently for latency, where lower is better:

```text
delta_ns = after_ns - before_ns
delta_percent = ((after_ns - before_ns) / before_ns) * 100
speedup = before_ns / after_ns
```

For throughput, where higher is better:

```text
delta_throughput = after_throughput - before_throughput
delta_percent = ((after_throughput - before_throughput) / before_throughput) * 100
```

Negative latency delta is an improvement. Positive throughput delta is an improvement.

### Mandatory iteration report

Save one Markdown report per iteration under
`libs/cxpr/benchmarks/results/ir-optimization/`. Each report must contain:

```text
Iteration: <n>
Hypothesis: <expected cause and effect>
Changed: <files and one to three logical tweaks>
Benchmark command: <exact command>
Environment: <commit, compiler, flags, CPU, build type>
Before: <median, p95, throughput and relevant counters>
After: <median, p95, throughput and relevant counters>
Delta: <absolute delta, percentage delta and speedup>
Correctness: <tests and parity result>
Assessment: keep/reject
Next hypothesis: <one concrete follow-up>
```

Include a compact comparison table:

| Benchmark | Before median | After median | Delta | Delta % | Speedup | Decision |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| `<case>` | `<ns>` | `<ns>` | `<ns>` | `<%>` | `<x>` | keep/reject |

Never report only a speedup ratio. Preserve raw before/after values and both absolute and
percentage deltas.

### Benchmark controls

Before each iteration:

1. Run all CXPR tests, including ASan/UBSan configurations.
2. Run `cxpr_bench_ir`, `cxpr_bench_model`, `cxpr_bench_engine`,
   `cxpr_bench_index_history`, and resample benchmarks.
3. Record at least nine trials and report median and p95 where the harness supports it.
4. Pin CPU frequency/governor when possible and record compiler, flags, CPU, and commit.
5. Compare instruction count, branches, branch misses, cache misses, and cycles with `perf`
   for the main benchmark cases.
6. Store raw results alongside the iteration report under
   `libs/cxpr/benchmarks/results/ir-optimization/`.

Do not keep a change based only on a synthetic microbenchmark. It must preserve or improve
at least one representative model/engine workload without materially regressing others.
Use a minimum of nine measured trials unless the benchmark documents why a larger sample is
required. Treat changes inside normal run-to-run variance as no improvement.

## Phase 0: lock semantic and performance baselines

### Work

- Add parity fixtures covering scalar, boolean, int64, short-circuit, errors, host calls,
  defined calls, producer structs, arrays, index/history, and nested lookbacks.
- Compare AST, existing IR, optimized IR, generated C, and CUDA where supported.
- Add benchmark cases for repeated fields from one producer and repeated subexpressions
  across multiple strategy expressions.
- Record IR instruction count and fallback count per fixture.

### Acceptance

- All backends produce equivalent results and errors for the fixture set.
- Benchmarks can distinguish compile/setup time from per-evaluation time.
- The harness reports when execution falls back to typed or AST evaluation.

## Phase 1: resolved input slots

### Design

Introduce internal lowered operations equivalent to:

```text
LOAD_VAR_SLOT_F64 slot
LOAD_PARAM_SLOT_F64 slot
LOAD_LOCAL_F64 slot
```

Names and hashes remain compiler metadata. At session creation, resolve input descriptors to
stable slots or guarded direct references. Structural context changes invalidate and rebind
the session binding; ordinary value updates do not.

Do not store context-specific pointers in a globally shared compiled program.

### Work

- Add an evaluator/session binding table keyed by lowered input descriptor.
- Reuse the validity model from `cxpr_context_slot` where appropriate.
- Resolve each unique variable and parameter once, not once per IR instruction.
- Deduplicate repeated input loads during lowering where liveness permits.
- Retain the current cached name lookup as a compatibility fallback for overlays and dynamic
  contexts that cannot provide stable slots.

### Likely files

- `libs/cxpr/src/ir/internal.h`
- `libs/cxpr/src/ir/compile/`
- `libs/cxpr/src/ir/exec/`
- `libs/cxpr/src/context/slot.c`
- `libs/cxpr/src/engine/session.c`

### Acceptance

- No hash or string comparison occurs for a successfully bound scalar input during a normal
  tick.
- Context mutation and overlay tests retain current behavior.
- `context_churn`, scalar expressions, and engine benchmarks improve without regressions in
  generated code.

## Phase 2: typed lowered IR

### Design

Separate semantic IR from executable lowered IR. Type inference selects explicit operations:

```text
ADD_F64      MUL_F64      DIV_F64
ADD_I64      CMP_GT_F64   CMP_EQ_I64
NOT_BOOL     AND_BOOL     RETURN_F64
```

The scalar executor must not construct `cxpr_value` values. Unsupported dynamic operations
may use the existing typed evaluator, but fallback boundaries must be explicit and counted.

### Work

- Add a closed internal value-kind enum to every virtual value.
- Lower generic arithmetic and comparisons after type checking.
- Reject invalid combinations before execution.
- Preserve exact division-by-zero, conversion, NaN, and comparison semantics.
- Split or specialize stack storage for `double`, `int64_t`, and boolean values.
- Remove per-opcode runtime type checks from fully typed programs.

### Acceptance

- Scalar, boolean, and int64 fixtures execute without `cxpr_value` construction.
- No AST fallback occurs for supported typed scalar programs.
- All parity and sanitizer tests pass.

## Phase 3: producer and struct-field lowering

### Design

Do not materialize and repeatedly inspect general struct values in the hot path. Resolve
producer output schemas during compilation and lower fields to typed result slots:

```text
CALL_PRODUCER_TYPED producer_id, args, result_base
LOAD_RESULT_F64 result_base + field_offset
```

When only one field is used and the provider supports it, allow a direct field operation.
When several fields are used, call the producer once and retain all results in typed slots.

### Work

- Intern producer and field identifiers during compilation.
- Deduplicate equal producer calls within one evaluation/tick.
- Flatten statically known record layouts into typed result slots.
- Avoid string-based `GET_FIELD` after successful lowering.
- Preserve a generic struct path for genuinely dynamic values.
- Share the layout contract with generated C/CUDA codegen.

### Acceptance

- The repeated-field benchmark performs one producer call per unique argument set.
- Known-field execution performs no field-name lookup or struct allocation.
- Typed struct benchmarks improve by at least an order of magnitude before proceeding.

## Phase 4: lookback and history specialization

### Design

Replace the general sequence:

```text
LOOKBACK_PUSH
LOOKBACK_RESOLVE
LOOKBACK_POP
```

with resolved operations where the target and offset are known:

```text
LOAD_HISTORY_F64 series_slot, constant_offset
LOAD_HISTORY_DYNAMIC_F64 series_slot, offset_register
```

Constant lookbacks should become direct ring-buffer indexing with prevalidated capacity.

### Work

- Resolve series identity and field chain during compilation/session binding.
- Precompute constant offsets and required history depth.
- Use typed ring buffers and direct indices for resolved series.
- Deduplicate identical history loads in one evaluation.
- Retain the resolver callback only for dynamic host capabilities.
- Preserve missing-history and out-of-range behavior exactly.

### Acceptance

- Constant lookback performs no AST traversal, name lookup, or resolver dispatch.
- `lookback_leaf`, `lookback_mixed`, and index-history benchmarks materially improve.
- Multi-step engine and generated-C parity remains exact.

## Phase 5: optimizer passes

Run optimization after semantic/type validation and before executable lowering.

### Initial passes

1. Constant folding.
2. Constant propagation.
3. Algebraic simplification that preserves CXPR floating-point semantics.
4. Dead-code elimination.
5. Unreachable-branch elimination.
6. Common-subexpression elimination within one program.
7. Inlining of small defined functions.
8. Deduplication of producer calls and lookback loads.
9. Hoisting of session-invariant parameters and constants.

Each pass must have a standalone test suite and an IR dump before/after form suitable for
debugging. Do not apply transformations such as reassociation unless the language contract
explicitly permits the resulting floating-point differences.

### Acceptance

- Optimized and unoptimized execution have semantic parity.
- Instruction counts decrease on `large_arith`, `large_branch`, defined-call, and repeated
  subexpression fixtures.
- Compile-time growth remains bounded and is reported separately from runtime improvements.

## Phase 6: register IR

### Design

Introduce a typed, three-address register IR for optimization and code generation:

```text
r0 = load_var_slot_f64 close
r1 = load_var_slot_f64 ema
r2 = sub_f64 r0, r1
r3 = load_var_slot_f64 atr
r4 = div_f64 r2, r3
ret r4
```

This becomes the canonical optimized representation. The interpreter may execute it directly
or lower it further to compact bytecode. Generated C/CUDA should consume the same register IR
to prevent backend drift.

### Work

- Add virtual-register allocation and explicit basic blocks.
- Build def-use information.
- Implement liveness analysis and temporary-slot reuse.
- Represent short-circuiting with explicit control-flow edges.
- Migrate codegen incrementally, retaining the old path behind a test-only comparison mode.

### Acceptance

- Every supported expression can be dumped and validated as register IR.
- C/CUDA output retains parity.
- The register executor does not regress scalar benchmarks relative to the Phase 5 result.

## Phase 7: compact executable bytecode

The current `cxpr_ir_instr` may occupy 48 bytes even for operand-free operations. After
optimization, lower register IR to a compact execution format with operands stored as small
indices and larger data in side tables.

Illustrative format only:

```c
typedef struct cxpr_bc_instr {
    uint8_t opcode;
    uint8_t dst;
    uint8_t lhs;
    uint8_t rhs;
    uint32_t operand;
} cxpr_bc_instr;
```

The final format must be selected from measurements, not from this sketch. Consider separate
fixed-width formats for common operations and an extended operand table for uncommon calls.

### Work

- Move strings, constants, callbacks, and producer descriptors into immutable pools.
- Keep hot instruction data contiguous.
- Measure 8-, 12-, and 16-byte instruction formats if needed.
- Preserve the logical public IR view through a translation adapter.
- Retain computed-goto dispatch on supported C compilers and a portable switch fallback.

### Acceptance

- Reduced instruction-cache misses and cycles are demonstrated with `perf`.
- Runtime memory per compiled expression decreases.
- Public IR inspection tests continue to pass.

## Phase 8: cross-expression strategy fusion

Once single-program lowering is stable, allow the engine to compile related aliases,
expressions, watches, and indicator dependencies into one tick plan.

### Work

- Construct one dependency graph for all expressions in a session.
- Evaluate shared inputs, indicators, producer calls, aliases, and lookbacks once per tick.
- Apply CSE across expression roots.
- Keep each public expression result addressable by a stable result slot.
- Preserve watch ordering and event semantics.

### Acceptance

- Shared work is executed once per tick.
- Strategy and model benchmarks improve without changing public engine behavior.
- Fusion can be disabled for differential testing.

## Superinstructions

Only evaluate superinstructions after typed slots, producer flattening, lookback
specialization, and compact bytecode are measured. Candidate patterns include:

```text
CMP_VAR_SLOT_CONST_GT
MUL_ADD_F64
LOAD_HISTORY_CMP_CONST
BRANCH_IF_VAR_SLOT_FALSE
```

Generate candidates from real opcode-frequency traces. Do not manually grow a combinatorial
opcode set. Keep a minimum frequency and measured speedup threshold for every retained
superinstruction.

## Testing strategy

For every phase:

- differential tests: AST versus old IR versus new IR versus generated C;
- property tests for arithmetic, comparisons, and control flow;
- malformed-program validation tests;
- context mutation, reallocation, overlay, clone, and clear tests;
- concurrent evaluation of one compiled program with independent sessions;
- producer callback count and ordering tests;
- lookback boundary and missing-history tests;
- ASan, UBSan, and leak checks;
- fuzzing of source-to-IR compilation and old/new executor parity.

Optimizations must be feature-gated internally during migration so tests and benchmarks can
compare old and new execution in one build. Remove each old path only after parity and
performance gates are met.

## Performance targets

Targets are directional gates, not permission to weaken semantics:

| Area | Initial target |
| --- | --- |
| Simple scalar expressions | no worse than 5x generated C |
| Large scalar arithmetic | no worse than 3x generated C |
| Defined functions | at least 2x faster than current IR |
| Constant lookback | at least 5x faster than current IR |
| Known producer fields | at least 10x faster than current typed IR |
| Engine overhead | remain at or below 15% over the low-level evaluator |
| Per-tick allocations | zero for fully lowered programs |
| Name/hash lookups | zero for successfully bound steady-state inputs |

Revisit targets after Phases 3 and 5 using the new baseline. Do not claim generated-C parity
unless the IR has been compiled to native machine code; interpreter dispatch has an inherent
cost.

## Rollout and rollback

1. Add the new representation alongside the current executor.
2. Compile both forms in debug/differential configurations.
3. Compare results and observable callbacks at runtime in tests.
4. Enable the new path per supported feature class: scalar, bool, int64, producer fields,
   then lookback.
5. Fall back to the current typed evaluator for unsupported operations.
6. Record fallback reasons and drive them toward zero.
7. Remove legacy execution paths only when all parity, sanitizer, and benchmark gates pass.

Each phase should be a separate reviewable change. If a phase regresses representative
workloads or complicates semantics without meeting its performance gate, revert that phase
without blocking the remaining independent work.

## Recommended implementation order

1. Phase 0: baseline and differential harness.
2. Phase 1: resolved input slots.
3. Phase 3: producer/field flattening.
4. Phase 4: specialized history access.
5. Phase 2: complete typed lowering for remaining operations.
6. Phase 5: optimization passes.
7. Phase 6: canonical register IR.
8. Phase 7: compact executable bytecode.
9. Phase 8: cross-expression fusion.
10. Consider profile-driven superinstructions only after the preceding work is measured.

The order intentionally attacks the measured lookup, struct, and lookback costs before a
larger IR representation rewrite. Register IR and compact bytecode should be justified by
profiles collected after the earlier bottlenecks are removed.

## Definition of done

- Public CXPR APIs and expression semantics remain compatible.
- All CXPR and Dynasty integration tests pass.
- Sanitizer and fuzzing runs report no new defects.
- Fully lowered steady-state evaluation allocates no memory and performs no name lookup.
- Producer fields and constant lookbacks use specialized typed operations.
- The optimizer and executable representation have deterministic debug dumps.
- Generated C/CUDA and IR consume one canonical typed representation or have enforced
  differential parity.
- Representative benchmark results meet the revised targets and are committed with the
  implementation.
