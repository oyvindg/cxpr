# Bulk/Grid Execution Plan

Status date: 2026-09-12

## Goal

Run compute-heavy, deterministic simulations whose local mathematics is
defined in `.cxpr` and generated as C or CUDA, while keeping cxpr independent
of physics, grids, devices, and scheduling policy.

The architectural boundary is:

```text
.cxpr scalar model -> typed/fused IR -> generated C/CUDA scalar function
                                         ^
host topology + buffers + scheduler -----|
```

## Current status

Available before this plan:

- typed model inputs, parameters, state, functions, imports, and outputs;
- compiled/fused scalar IR and deterministic per-tick state transitions;
- generated C descriptor ABI with named scalar inputs and outputs;
- CUDA device-source generation for the supported scalar/history subset;
- arrays in the reference runtime, plus host extension points for functions,
  sources, indexing, and code generation;
- small physics/scientific examples, but no reusable bulk execution contract.

Important gaps:

- generated execution is one model invocation at a time;
- no standard binding from host-owned SoA buffers to model inputs/outputs;
- CUDA emits a device evaluator, but launch geometry and buffer ownership are
  not described by a shared contract;
- no capability manifest tells a host whether every extension can lower to a
  requested backend;
- no end-to-end grid example proves `.cxpr` -> C -> bulk parity yet.

The standalone cxpr test baseline was 114/114 passing before bulk work began.
Legacy `cxpr-cxta-adapter` is explicitly outside this plan.

## Design rules

1. cxpr must not know what a cell, particle, field, tensor, metric, or boundary
   condition means.
2. A `.cxpr` model defines one logical element's pure/local calculation.
3. The host owns topology, neighbor gathering, memory allocation, time loops,
   convergence checks, device selection, and synchronization.
4. Complex values and tensors are initially represented as named scalar
   components. Native aggregate ABI work requires measured evidence first.
5. C and CUDA must use the same ordered model descriptor and produce parity
   within documented floating-point tolerances.
6. Every backend-visible host extension must declare lowering support; missing
   lowering is a compile-time diagnostic, never a silent CPU fallback.

## Milestones

### M1 — generic CPU bulk contract

- [x] Add public strided SoA input/output views.
- [x] Give each logical element an independent generated-model state block.
- [x] Support range execution so a host can partition work across threads.
- [x] Validate descriptor, schema, buffers, state stride, and ranges.
- [x] Test strided mapping, isolated state, and partial ranges.
- [ ] Add generated `.cxpr` -> C -> bulk end-to-end parity fixture.

### M2 — topology-neutral grid fixture

- [ ] Add a Klein–Gordon 1D example with double-buffered host fields.
- [ ] Materialize left/center/right values as ordinary scalar columns.
- [ ] Put periodic/fixed boundary choices entirely in the host.
- [ ] Check reference runtime, generated scalar C, and bulk C parity.
- [ ] Track energy drift and convergence when `dt`/`dx` are refined.

### M3 — CUDA bulk artifact

- [ ] Define a stable CUDA bulk artifact descriptor alongside generated source.
- [ ] Generate a kernel wrapper mapping one thread to one logical element.
- [ ] Keep state and all field buffers resident across steps.
- [ ] Support host-selected launch geometry and stream without CUDA types in
  the core API.
- [ ] Test generated source shape without requiring a GPU.
- [ ] Add optional NVCC/runtime parity tests when CUDA is available.

### M4 — backend extension contract

- [ ] Publish extension capabilities for reference, IR, C, and CUDA lowering.
- [ ] Add explicit C/CUDA emit hooks for host-defined elementary operations.
- [ ] Reject unsupported callbacks during code generation with source spans.
- [ ] Document requirements for pure, deterministic, thread-safe operations.

### M5 — physics-oriented fixtures, not physics primitives

- [ ] Complex wavefunction using `psi_re` and `psi_im` scalar components.
- [ ] Schrödinger 1D fixture with normalization diagnostics.
- [ ] Multi-component field fixture demonstrating host-materialized tensors.
- [ ] Fixed curved-background fixture with metric components supplied by host.
- [ ] Benchmarks for scalar evaluator, bulk C, and CUDA kernels.

## Public API boundary

Core cxpr should expose only generally useful primitives:

- scalar model descriptors and type metadata;
- strided/broadcast bulk columns;
- independent state stride and range execution;
- backend capability/lowering metadata;
- deterministic math semantics and clear floating-point guarantees.

Grid coordinates, dimensions, halos, sparse meshes, MPI, CUDA streams, tensor
layout, units, and physical constants remain host concerns. A reusable helper
library may be built on this API, but it must not become cxpr core semantics.

## Acceptance criteria

- A host can define local update equations in `.cxpr`, generate C/CUDA at
  build time, and run them over at least one million logical elements without
  reparsing or allocating per element.
- CPU range partitions are race-free when their state/output ranges are
  disjoint.
- Generated C and CUDA agree with reference evaluation for documented fixtures.
- Unsupported operations fail during generation with actionable diagnostics.
- cxpr public headers contain no domain-specific physics or grid vocabulary.

