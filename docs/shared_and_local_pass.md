# Shared and Local Passes

Status: design sketch (syntax proposal)

Companion to [read_only_peer_state.md](read_only_peer_state.md). That document
adds per-element peer reads (a local pass over a frozen snapshot). This one adds
a **shared pass** that runs once per step and computes global quantities, so a
model can compute something over the whole field before it updates each element
locally.

The motivating case is field evolution that needs a global quantity first:
normalize a wavefunction by its total norm, couple every agent to a mean field,
or check a global residual — then step each element.

## The two phases

One step of a model runs two phases, in order:

```text
1. shared pass  : collectives over the previous snapshot -> shared values
2. local pass   : per-element update reading previous state, peers, and shared values -> next
```

Both phases read the same immutable `previous` snapshot. The local phase
additionally reads the shared values produced in phase 1. Only the local phase
writes `next`. Nothing reads `next` mid-step, so determinism is preserved
exactly as in the peer design.

This is deliberately **not** an arbitrary "global tick" over the whole buffer.
The shared phase is restricted to a closed set of deterministic collectives
(`reduce`, `scan`, `broadcast`) with a defined order and a backend-parity
contract. Arbitrary cross-element writes, sorts, solves, and dense linear
algebra are not part of it; see "What stays out" below.

## Confirmed mental model

- **Shared / felles:** computed once per step, visible to every element.
- **Local / lokal:** computed per element, as today, now able to read shared
  values and peers.
- **Order:** shared first (reads previous), then local (reads previous + shared,
  writes next).
- **Outer loop:** the host repeats steps and decides convergence. cxpr has no
  loops or recursion by design; it executes one step (both phases) per call.

## Syntax sketch

A model gains a single `shared { ... }` block that owns the whole shared phase:
its inputs (a nested `in { }`) and its collective results. The local phase stays
as the existing flat top-level sections, so a model with no `shared` block is
exactly a model of today.

```cxpr
model gross_pitaevskii

# ---- shared (collective) phase ----
shared {
    in { dt }                          # host-supplied global scalars
    norm2 = sum(re*re + im*im)         # collective sum over all elements
}

# ---- local (per-element) phase ----
peers { xm, xp, ym, yp, zm, zp }       # 3D face peers (6-connectivity)
in { potential }

$half = 0.5

# real/imaginary parts carried as two real fields (cxpr values are real doubles)
lap_re = xm.re + xp.re + ym.re + yp.re + zm.re + zp.re - 6*re
lap_im = xm.im + xp.im + ym.im + yp.im + zm.im + zp.im - 6*im

# normalized explicit step: divide by the global norm computed in the shared phase
inv = 1 / sqrt(shared.norm2)
re := (re + dt * ($half*lap_im - potential*im)) * inv initial re0
im := (im - dt * ($half*lap_re - potential*re)) * inv initial im0

out { density = (re*re + im*im) }
```

Reading conventions:

- The nested `in { dt }` declares shared inputs; they are read bare (`dt`), like
  params. The collective bindings are the shared *outputs* — no explicit `out`
  is needed inside `shared`.
- `shared.norm2` is the collective result, broadcast to every element.
- `sum(expr)` inside `shared` evaluates `expr` per element over the previous
  snapshot and reduces it with a defined order. `sum`, `min`, `max`, `any`, `all`
  are the reduce collectives. No `reduce` keyword: the `shared` context makes the
  aggregate a whole-field reduction.
- `re`/`im` and their peer reads (`xm.re`) work exactly as in the peer design;
  peer visibility is still inferred from the `slot.field` reads.
- Everything local (`:=`, `initial`, `fn`, `$params`, `out`) is unchanged.

### Grammar delta over the peer design

On top of the two additions in the peer document (`peers { }` and
`slot.field`), the shared pass adds:

1. **A `shared { ... }` block** containing an optional nested `in { ... }` for
   host-supplied global scalars and the collective bindings (the shared outputs).
2. **Collective aggregates** `sum`/`min`/`max`/`any`/`all` (reduce to scalar) and
   `cumsum` (prefix), usable only inside `shared`. No `reduce`/`scan` keyword.
3. **`shared.<name>` reads** in the local phase.

No control flow, no new numeric types. The collective set is closed and fixed.
The nested `in` requires the block parser to accept `in { }` inside `shared`, one
level of nesting the flat top-level grammar does not have today.

## Collective operators (closed set)

- `sum|min|max|any|all (expr)` — whole-field reduction to one scalar, broadcast
  to all elements. Defined reduction order for parity.
- `cumsum(expr)` — exclusive/inclusive prefix over element index, producing a
  per-element value (the one collective whose result is positional, not scalar).
  Useful for building quantiles/CDFs; use sparingly. It gets its own name because
  it is not a reduce-to-scalar.
- `broadcast` — implicit: any `shared.<name>` is readable everywhere in the
  local phase.

That is the entire set. If an algorithm needs a sort, a solve, an SVD, or a
tensor contraction, that is a host step between passes, not a collective.

**Name overload with R6.** `sum`/`min`/`max` already exist as R6 fixed-arity
folds over arguments (`sum(a, b, c)`). Inside `shared`, the same names applied to
an element expression are field collectives instead. This is safe because
`shared` is the only site where collectives are legal, and a single-argument R6
fold outside `shared` is degenerate (`sum(x) == x`), so no existing meaning is
lost. The context, not the name, selects the collective reading.

## Execution and determinism

```text
previous snapshot ─┬─► shared pass  ─► shared values ─┐
                   └─────────────────────────────────┴─► local pass ─► next ─► swap
```

- The shared pass reads only `previous`; it cannot see `next`.
- The local pass reads `previous`, its peers (from `previous`), and the shared
  values; it writes only its own `next` record.
- Shared reductions follow the same floating-point parity contract as the peer
  design: identical results require a fixed reduction tree and the documented
  contraction rules. A backend may otherwise leave a reduction to the host.
- Because both phases key off the same snapshot, the step is a pure function of
  `previous` + inputs — no dependence on thread scheduling or launch geometry.

## Host API tie-in

The shared phase produces a small shared-values block that the local phase
consumes. Sketch, extending `cxpr_bulk_pass_view` from the peer document:

```c
typedef struct cxpr_bulk_shared_view {
    const cxpr_value* shared_in;   /* host-supplied global scalars   */
    size_t shared_in_count;
    cxpr_value* shared_out;        /* filled by the shared pass, read by the local pass */
    size_t shared_out_count;
} cxpr_bulk_shared_view;

/* One full step: shared pass, then local pass. */
cxpr_bulk_status cxpr_bulk_step_shared_local(
    const cxpr_generated_model_descriptor* descriptor,
    cxpr_bulk_pass_view* pass,
    cxpr_bulk_shared_view* shared,
    const cxpr_bulk_view* inputs_and_outputs);
```

The generated model exposes two entry points — a collective `reduce`/`scan`
kernel and the `tick_peers` local kernel — sequenced by the driver. On CUDA both
run as separate launches; the kernel boundary provides the global
synchronization between phases.

## Composition: richer orderings

A single model runs shared-then-local. Some algorithms need
local-then-shared-then-local within one step (produce a candidate, reduce it,
correct it). Two ways to express that, both keeping cxpr's contract:

- **Two models sequenced by the host:** local pass A → collective → local pass B.
- **A future multi-pass model** that declares an explicit ordered list of
  passes. Not proposed yet; the two-phase shared/local form covers the common
  case.

Worked example — one conjugate-gradient iteration for a Dirac/Laplacian solve,
which is exactly this composition:

```text
matvec    : local/peer pass   Ap = A · p          (A is a peer stencil)
dot       : shared reduce      pAp = sum(p*Ap)
axpy      : local pass         x := x + (rr/pAp)*p ;  r := r - (rr/pAp)*Ap
newdot    : shared reduce      rr_new = sum(r*r)
```

The host runs this sequence in its CG loop until `rr_new` is small. Every matvec
is a peer pass, every dot product is a shared reduce, every axpy is a local
pass. This is how dynamical-fermion solves become mostly expressible in cxpr,
with only the scalar iteration control left in the host.

## What stays out

Even with the shared pass:

- **Control flow / the outer loop** — Markov chains, time stepping, CG
  iteration — stays in the host. cxpr executes one step per call.
- **Dense linear algebra** — solves, SVD, tensor contraction — is a host step
  using native libraries, outside cxpr's parity contract.
- **Exponentially large state** — full many-body Hilbert space, large
  state-vector circuit simulation — is outside the element-array data model
  entirely, unchanged by this proposal.

## Open questions

- Should `shared` results feed the *next* step (like state) or only the current
  local phase? The sketch above uses current-step only, which keeps the pure
  snapshot semantics; feeding forward is just ordinary state.
- Collective spelling: context-scoped aggregates (`sum(expr)` inside `shared`,
  overloading the R6 names) versus distinct names (`sum_all`, `field_sum`). The
  sketch takes the context-scoped route; the tradeoff is overload clarity versus
  verbosity. Whether `cumsum` earns its place in the first version or is deferred
  with the variable-arity collection form is still open.
- Whether the nested `shared { in { } }` scalars should be modeled as ordinary
  params (`$name`) instead; params may already cover this.
- Whether to wrap the local phase in a symmetric `local { ... }` block. It reads
  well and makes the two phases structural, but it would restructure the flat
  model grammar; keeping local flat preserves backward compatibility, so it is
  deferred.
- Whether `shared` needs its own persistent state (`shared { state x = 0 }`).
  Not for v1: collective results are ephemeral (recomputed each step from the
  snapshot), and any global value that must persist across steps is already the
  host's — it owns the outer loop, supplies `shared { in }` each step, and can
  accumulate a collective result between steps. The only real motivation is a
  device-resident accumulator that avoids host round-trips on CUDA. If added
  later the semantics are clean: a single-writer cell updated once per step in
  the shared phase via `:=`, double buffered, read as `shared.x` in both phases.
- Reduction determinism level: bit-identical fixed-tree everywhere, or
  documented-order with a parity-proof gate like R6.
