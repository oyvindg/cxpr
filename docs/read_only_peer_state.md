# Read-Only Peer State

Status: design proposal

## Purpose

This document proposes a topology-agnostic way for a cxpr bulk model to read
state belonging to other logical elements. The main use case is a parallel
local update of the form:

```text
next[i] = f(previous[i], previous[peer_0], ..., inputs[i])
```

Examples include flow-field pathfinding, cellular automata, diffusion, wave
simulation, image filters, influence maps, graph relaxation, and local agent
interaction.

The feature must not introduce grids, directions, pathfinding, or another
domain-specific topology into the cxpr language. A host supplies the topology;
the cxpr backend supplies deterministic snapshot execution.

## Current limitation

A bulk model currently has independent state for every logical element. It can
read and update its own state, but it cannot directly read another element's
state. A host therefore has to gather peer values into ordinary input
columns before every pass.

For example, the current pathfinding fixture receives `d_n`, `d_s`, `d_e`, and
`d_w` as scalar inputs. This works and preserves cxpr's existing abstraction,
but it makes the host responsible for repeated halo gathering, iteration,
buffer swapping, and convergence checks.

`fn`, `state`, and `use` do not remove this limitation:

- `fn` expresses reusable local calculations.
- `state` stores history owned by one model instance.
- `use` composes or imports models.
- none of them identifies another bulk element or grants access to its state.

## Proposed execution model

Bulk execution should distinguish three concepts:

```text
own previous state  -> read-only during a pass
peer state          -> read-only from the same previous snapshot
own next state      -> writable only by the current element
```

Execution uses two state buffers:

```text
previous snapshot -> parallel evaluation -> next snapshot -> swap
```

The snapshot normally does not require a physical copy. Buffer A remains
immutable while a pass writes buffer B. The backend swaps their roles only
after every element in the pass has completed.

This is Jacobi-style execution. It is deterministic because no element can
observe values written by another element during the same pass.

It also matches existing cxpr semantics. A model's `state` combined with a
staged `:=` update is already a per-element Jacobi step: expressions observe the
old value and the update becomes visible on the next tick. Peer reads only
extend that same "read the previous snapshot" rule from an element's own state
to another element's state. Nothing new is introduced conceptually; the read set
is widened.

### Peer-readable state is the model's own state

Peer slots read the same fields a model declares as state. In the example
below, `n.dist` reads the peer's `dist`. There is no separate "peer
state" category; there is one state layout, observed either as your own (previous
value) or as a peer's (previous value from another element).

**Visibility is inferred, not declared.** A bulk pass runs one model over every
element, so the compiler already knows which state fields are read through a
peer slot: `dist` is peer-visible precisely because `n.dist` (and its
siblings) appear in the model. No `export`/`shared` keyword is required for the
homogeneous case. An explicit modifier would only be needed later for
heterogeneous topologies, where a peer is a different model, or to freeze
visibility as a declared interface independent of body analysis. Prefer
inference now; keep an explicit marker as a future option, and if one is added
prefer a pull-oriented word such as `shared` or `readable` over `export`, which
implies a directed send and overlaps `use`/module vocabulary.

Two consequences follow:

- **Only peer-visible fields are double buffered.** Today each element owns
  its state block and updates it in place. Peer reads require the *visible*
  fields to live in a globally addressable previous buffer that other elements
  read, so those fields are double buffered (buffer A read, buffer B written,
  then swap). Fields never read through a slot stay element-private and can keep
  the existing in-place path. An implementation may double buffer the whole
  record for layout simplicity, but only the visible subset requires it. Models
  that declare no peers are unchanged.
- **The visible set is statically knowable.** Because it is derived from the
  fixed set of `slot.field` reads in the model, the manifest and the generated
  evaluator expose exactly those fields, with no dynamic access pattern.

## Responsibility boundary

### The host owns

- the number and identity of elements;
- initial state and ordinary model inputs;
- the mapping from logical peer slots to element indices;
- topology changes and versioning;
- invalid or missing-peer policy;
- the outer stopping policy, such as convergence or a maximum step count;
- domain behavior such as collision handling, reservations, and route caches.

### The cxpr backend owns

- read-only access to the previous state snapshot;
- indexed lookup of peer state;
- parallel evaluation over elements;
- writes to each element's next state only;
- synchronization between passes;
- state-buffer swapping;
- optional generic reductions such as `max(abs(next - previous))`;
- identical observable semantics across the reference, IR, generated-C, and
  CUDA backends.

The backend may execute one pass or provide a reusable step driver. It must not
interpret peer slots as north, south, graph edges, or any other topology.

## Conceptual model syntax

The final syntax is intentionally left open. A possible form is:

```cxpr
model flow_field_cell

in { cost, is_wall, is_goal }
peers { n, s, e, w }

$INF = 1e18
fn sadd(a, b) = min(a + b, $INF)

cand_n = sadd(n.dist, cost)
cand_s = sadd(s.dist, cost)
cand_e = sadd(e.dist, cost)
cand_w = sadd(w.dist, cost)

next_dist = is_wall > 0 ? $INF
          : is_goal > 0 ? 0
          : min(cand_n, cand_s, cand_e, cand_w)

dist := next_dist initial $INF

out {
    next_dist,
    dir = argmin(cand_n, cand_s, cand_e, cand_w)
}
```

Here `n`, `s`, `e`, and `w` are logical slots. Their names carry no built-in
meaning. The host binds each slot to an element index for every element.

`dist` uses the existing combined form `name := update initial init`
(parser.c): one line for declaration, initial value, and staged update. No
visibility keyword appears because the compiler infers that `dist` is
peer-visible from the `n.dist`/`s.dist`/... reads above. If a later design
adds an explicit marker for heterogeneous topologies, note that the combined
form takes no `state` keyword, so it would attach as a prefix
(`shared dist := next_dist initial $INF`), never as `... state dist := ...`.

### Base-language surface changes

The grammar delta is deliberately small. The fixed-slot form adds exactly two
things to the base language:

1. **A `peers { ... }` declaration.** A new keyword and block alongside the
   existing model keywords (`in`, `out`, `state`, `use`, `fn`, `update`,
   `meta`). It lists the slot names.
2. **`slot.field` reads in expressions.** cxpr has no general `ident.field`
   member access today (only dotted `$param` names are passed through), so
   reading a peer's state is a new expression form or binding rule.

Everything else in the example is reused unchanged:

- `dist := next_dist initial $INF` — the existing combined declaration + update.
- `argmin`, `min`, `fn sadd`, `$INF` — existing R6 builtins, `fn`, and params.
- `in { ... }`, `out { ... }` — existing declarations.

Slot names (`n`, `s`, `e`, `w`) and state field names (`dist`) are user-defined
identifiers with no built-in meaning, exactly like ordinary bindings; the two
additions above are the whole grammar surface. The variable-arity collection
form would add more (a peer-collection type and a reduction over a runtime
count), but that is deferred with its phase.

The weight of the feature is therefore not in the grammar. It is in the
double-buffered pass semantics, the `tick_peers` ABI, IR/C/CUDA lowering,
the host API, and validation. Those sections below are where the design risk
lives.

### Two topology classes, not two arbitrary syntaxes

The real axis is peer *arity*, not slot names. The names `n`, `s`, `e`, `w`
are arbitrary labels with no built-in meaning; do not read them as compass
directions. A grid happens to have four fixed peers, but that is a property
of the host's topology, not of this feature.

That distinction matters because the applications this document lists span both
fixed-arity and variable-arity topologies:

- **Fixed arity.** Every element has the same, statically known number of
  peers: grid stencils, cellular automata, image convolution, fixed-radius
  filters. Named slots (`peers { n, s, e, w }`) fit these directly and match
  the existing 4-nabo and 8-nabo fixtures and the B4 canonical ordering.
- **Variable arity.** Peer count differs per element: general graph
  relaxation, label propagation, connected components, PageRank-like iterations,
  irregular meshes, and agent models with varying local sets. Named slots do not
  fit these at all. They need a bounded peer *collection* with a per-element
  count `k <= K`, reduced over the live `k`.

  Note this needs more than R6. R6's `sum`/`argmin`/`argmax` are fixed arity
  (1-8 arguments) and lower to unrolled nested ternaries; they cannot consume a
  runtime `k`. The collection form therefore requires a new
  reduction-over-collection construct with a static bound `K` and a dynamic live
  count, plus a defined order for determinism. That is real added language and
  lowering scope, and is the main reason to phase fixed arity first.

So the collection form is not a distant "maybe generalize later"; it is
required by applications already claimed here. The naming choice does not cover
all scenarios, and the document should not pretend a single fixed-slot syntax
does.

### Dimensionality is a host concern

3D (or N-D) peers need no new cxpr concept, which is the point of keeping
topology in the host. cxpr sees a flat array of elements and an index table; it
has no notion of 2D, 3D, axes, or coordinates. The host flattens its xyz grid to
a linear element index (for example `i = x + y*W + z*W*H`) and fills
`peer_indices` with whatever adjacency it wants:

- 6-connectivity (face peers): `peers { xm, xp, ym, yp, zm, zp }`;
- 18- or 26-connectivity (edges and corners): more fixed slots;
- anisotropic or sparse 3D adjacency: the variable-arity collection form.

Nothing in the language, IR, generated C, or CUDA changes. A 3D face-peer
stencil is just a fixed-arity model with six slots; a 3D distance transform is
the flow-field model with six slots and six per-axis costs. If adding a
dimension required a language change, the topology would have leaked into cxpr —
so 3D working unchanged is a correctness check on this design, not a special
case to support.

### Recommendation: phase fixed arity first, but design for both

Implement fixed named slots first because they are smaller and unblock the grid
pathfinding case immediately. Separate the two things the collection form will
later need:

- **The peer read** — a strided load from the previous buffer at a resolved
  index. Fix this lowering now so the collection form reuses the exact same
  primitive; the fixed-slot form is just the special case `k == K` with unrolled
  slots. Do not design a read that only works for fixed names.
- **The reduction over peers** — with fixed slots this is plain R6 over
  named arguments, so nothing new is needed. The collection form additionally
  needs the runtime-`k` reduction construct noted above. That part is genuinely
  new work and is deferred with the collection syntax; only the read primitive
  has to be forward-compatible from day one.

Both forms must have statically knowable bounds (`K`) for generated C and CUDA
and must reject writes through peer references. For fixed slots the model,
not the host, fixes the count, so the `peers_per_element` field in the host
API below must equal the model's declared slot count. For the collection form it
equals the declared maximum `K`, with the live per-element `k` supplied by the
host alongside the index table.

## Conceptual host API

The public API should describe buffers and topology without domain vocabulary.

**Naming.** As prose, "snapshot" is used throughout this document for the
immutable previous-state view, which is fine. The constraint is narrower: the C
*identifier* `cxpr_bulk_snapshot` is already taken
(`include/cxpr/bulk_snapshot.h`) for versioned serialization of state to bytes,
an unrelated concept. New API symbols must therefore not be named
`cxpr_bulk_snapshot*`. They use *pass* instead: one Jacobi pass over the
double-buffered state.

**Relationship to `cxpr_bulk_view`.** The existing `cxpr_bulk_view` already
carries `states`, per-element `state_stride`, and `element_count`. The pass view
does not replace it; it wraps a second state buffer plus the topology table and
reuses the existing view for inputs, outputs, and params. The single state
buffer in `cxpr_bulk_view` becomes the pair (previous, next) below.

```c
typedef struct cxpr_bulk_pass_view {
    size_t element_count;      /* must equal inputs_and_outputs->element_count */

    const void* previous_state; /* read-only: own and peer reads */
    void* next_state;           /* write-only: each element writes its record */
    size_t state_stride;        /* must match the view's state_stride          */

    const uint32_t* peer_indices; /* element_count rows of slot indices    */
    size_t peers_per_element;     /* fixed slots: model's count; collection: max K */
    size_t peer_stride;           /* bytes between one element's slot row and the next */
    uint32_t invalid_peer;        /* sentinel index; see below             */

    /* Variable-arity (collection) form only; null for fixed named slots.
     * live count k <= peers_per_element for each element. */
    const uint32_t* peer_counts;
} cxpr_bulk_pass_view;

cxpr_bulk_status cxpr_bulk_step_pass(
    const cxpr_generated_model_descriptor* descriptor,
    cxpr_bulk_pass_view* pass,
    const cxpr_bulk_view* inputs_and_outputs);

/* Exchange previous/next roles. No copy; pointer swap only. */
void cxpr_bulk_pass_swap(cxpr_bulk_pass_view* pass);
```

This API is illustrative, not a compatibility commitment. The implementation
must also account for typed state layout, alignment, state initialization,
schema validation, and device-resident buffers.

Hosts that already materialize halo columns must remain supported. Indexed
pass reads are an additional execution mode, not a forced replacement.

## Generated tick ABI

This is the sharpest concrete change and must be settled before the syntax.

The current generated tick is:

```c
void tick(void* state, const cxpr_value* inputs,
          const cxpr_value* params, cxpr_value* outputs);
```

It has no way to reach another element's state, so peer reads need a second
entry point. Only models that declare `peers` emit it; models without
peers keep the existing `tick` and the existing in-place path untouched.

```c
void tick_peers(
    const void* previous_own,      /* this element's previous-state record   */
    void* next_own,                /* this element's next-state record        */
    const void* previous_state,    /* base of the previous-state buffer       */
    size_t state_stride,
    const uint32_t* peer_indices,  /* this element's slot row                 */
    size_t peers_per_element,
    uint32_t invalid_peer,
    const cxpr_value* inputs, const cxpr_value* params, cxpr_value* outputs);
```

A peer read `n.dist` lowers to: take the slot index for `n`, resolve it
against `previous_state` and `state_stride`, and load the peer-visible `dist`
field at its known offset. The generated evaluator never holds a writable
pointer into `previous_state`.

Introducing `tick_peers` bumps `CXPR_GENERATED_MODEL_ABI_VERSION` (currently
5). The descriptor gains an optional `tick_peers` pointer that is null for
non-peer models, so old hosts and old artifacts keep working. The exact
argument packing is open; the requirement is that a peer read resolves to a
strided load from the previous buffer with no re-evaluation and no per-backend
divergence.

## Invalid peers and boundaries

Missing peers must be explicit. There are two candidate mechanisms, but the
design should commit to one to avoid two code paths for a single concern:

1. **Primary: map the slot to a host-supplied sentinel element.** The host
   allocates one extra element whose peer-visible state holds neutral values (for
   flow fields, distance = the finite sentinel). Out-of-bounds and blocked slots
   point at it. The backend needs no branch, no per-field fallback binding, and
   no special index value; a sentinel peer reads like any other peer.
   This also matches how pathfinding already maps walls to a finite-sentinel
   cell.
2. **Fallback only if needed: `invalid_peer` plus a declared per-field
   default.** Kept in the API for now, but it adds a branch in the hot loop and
   a fallback-binding step. Prefer option 1 unless a concrete case requires it.

The backend must not silently clamp indices or assume periodic, reflecting, or
blocked boundaries. Those are host policies.

For flow-field pathfinding, a host can map blocked or out-of-bounds peers
to state whose distance is the finite sentinel value. A direction output is
valid only when the resulting distance is below that sentinel.

## Determinism and safety requirements

- Peer state is immutable for the complete pass.
- An element writes only its own next-state record.
- No generated evaluator may retain a writable pointer to previous state.
- The result must not depend on thread scheduling or launch geometry.
- Aliasing previous and next buffers is rejected unless a future execution
  mode defines safe in-place semantics explicitly.
- Peer indices and state layouts are validated before execution.
- Invalid indices produce a defined error or declared fallback, never
  unchecked memory access.
- Floating-point contraction and reduction order follow cxpr's documented
  backend parity contract.
- Unsupported state fields or dynamic access patterns fail during compilation
  or binding rather than falling back silently.

## Generated C and CUDA

Generated evaluators need a backend-neutral way to load a state field from a
known peer slot. The generated-C backend may lower this to a strided buffer
lookup. CUDA may lower the same operation to device-resident state and topology
buffers.

For CUDA, both state buffers and the peer-index table should remain on the
device across steps. A kernel reads buffer A, writes buffer B, and a subsequent
launch observes the swapped roles. Kernel boundaries provide the required
global synchronization.

The capability manifest should state whether a compiled model requires:

- snapshot state access;
- the maximum or exact number of peer slots;
- the state fields read through those slots;
- a convergence reduction;
- support for a particular generated backend.

## Optional reductions

A generic reduction can remove more host boilerplate without introducing
pathfinding semantics. Useful operations include:

```text
max(abs(next.field - previous.field))
sum(output.field)
any(output.changed)
all(output.valid)
```

This absorbs the deferred R8 bulk all-reduce; it is the same capability, not a
third mechanism. Reduction order and floating-point guarantees must be
specified. A backend may also leave reduction to the host; peer-state access
must not depend on the reduction API, and this feature must not block on R8
landing.

## Pathfinding example

For a flow field, the host supplies:

- one element per traversable or blocked cell;
- four or eight logical peer slots per element;
- traversal cost, wall status, and goal status;
- initial distance `0` for goals and a finite sentinel elsewhere.

The cxpr model performs one relaxation step and emits the next distance and a
direction index. The backend repeats snapshot steps. The host decides when to
stop and extracts routes by following direction indices.

Game-specific concerns remain outside cxpr:

- world and space identifiers;
- dynamic occupancy;
- movement reservations and collision resolution;
- cache invalidation after map changes;
- selection among goals;
- fallback or rerouting policy.

## Other applications

The same primitive supports:

- heat diffusion, waves, pressure relaxation, and stencil solvers;
- cellular automata, fire spread, vegetation, and epidemic models;
- influence, danger, sound, smell, visibility, and territory fields;
- image convolution, morphology, denoising, and distance transforms;
- graph relaxation, label propagation, connected components, and PageRank-like
  iterations;
- flocking, local social influence, and other peer-based agent models.

It is not intended to directly model algorithms requiring mutable shared
queues, cross-element writes, recursion, dynamic graph mutation, or
order-dependent in-place updates such as unrestricted Gauss-Seidel execution.

## Stochastic methods and deterministic randomness

Support for stochastic lattice methods is not a syntax question and cannot be
added by grammar alone. Local methods such as Metropolis and heat-bath updates
decompose into three parts:

- **Ordering (Gauss-Seidel sweeps) is usually avoidable.** Red-black /
  checkerboard coloring makes each half-sweep a pure peer pass: color-A elements
  update while reading color-B from the frozen previous buffer, then the active
  color changes for the next half-sweep. Inactive elements must be copied through
  unchanged: update A and copy B, swap buffers, then update B and copy A.
  Equivalently, the backend may provide a masked pass whose contract explicitly
  preserves inactive state. The coloring is host topology (a supplied mask), so
  it needs no language change and reuses the existing pass.
- **Accept/reject is an ordinary conditional** —
  `next = u < exp(-dS) ? proposal : current` — once a random draw `u` exists.
- **Randomness is the one genuine missing primitive, and it is a semantic
  decision, not a syntactic one.** A naive `rand()` breaks cxpr's core contract:
  independence from thread scheduling and launch geometry. The correct form is
  a counter-based RNG (Philox/Threefry) keyed from a declared `$seed` on
  `(element index, step, stream, draw)`. The final `draw` coordinate, or an
  equivalent stable call-site identifier, keeps multiple draws in one step
  distinct without making them depend on expression evaluation order. This is
  a runtime/semantic feature with only a small syntactic surface, such as a
  keyed `rand_uniform` builtin.

The RNG contract guarantees identical random bits across the reference, IR, C,
and CUDA backends. It does not by itself guarantee bit-identical final model
results when a distribution transform or model uses backend math such as
`exp`, `log`, `sin`, or `cos`, nor when a parallel floating-point reduction has
an unspecified tree. Those results remain governed by cxpr's floating-point
contract. Full bit identity would additionally require deterministic math
implementations and a fixed reduction order.

Consequence: reproducible counter-based randomness unlocks independent-path
Monte Carlo and local stochastic updates such as bosonic Metropolis and
checkerboard heat-bath passes. It does not by itself unlock the entire
stochastic class. HMC additionally requires trajectory-level orchestration,
global action/Hamiltonian reductions, a reversible integrator, and global
accept/reject; dynamical fermions also require nonlocal Dirac-operator solves.
Checkerboard coloring remains host topology, local accept/reject is an ordinary
conditional, and global observables remain host reductions. This RNG work is a
separate optional track, not part of the implementation phases below.

## Forward Monte Carlo (independent paths)

Forward Monte Carlo — the kind used for option pricing, VaR/CVaR, and portfolio
scenario generation — is a different structure from the lattice Monte Carlo
above, and an important one to call out because it does **not** use peer state
at all.

Independent paths do not interact, so there is no topology between them: one
path is one element, and there are no peers. The structure the paths do need is
already in the language:

- **Per-path state stepped over time.** `S(t+dt) = f(S(t), draw)` is a
  recurrence, which is exactly `state` plus a staged `:=` update.
- **Bulk parallelism over paths.** The existing `cxpr_bulk_run` path already
  runs one model over many elements.
- **Path-dependent payoffs** (Asian, barrier, lookback) accumulate running
  statistics — average, maximum, a barrier-hit flag — in ordinary per-path
  state as the recurrence advances.

The only missing primitive for per-path propagation is the same counter-based
randomness described above, and it is cleanest here: because paths are
independent, keying the RNG on `(path index, step, stream, draw)` needs no
cross-element coordination. There is no accept/reject and no Gauss-Seidel
ordering, so none of the lattice caveats apply.

Two operations sit at the boundary and remain host or reduction concerns:

- **Aggregation.** Pricing is a discounted mean (the `sum` reduction). VaR/CVaR
  is a quantile — an order statistic that is more than `sum`/`max`, so it is
  either a host sort or a dedicated quantile reduction.
- **Cross-path steps.** Longstaff-Schwartz regression for American/Bermudan
  options and resampling in sequential Monte Carlo are global operations across
  all paths at a step (a least-squares fit; a multinomial gather/scatter). The
  per-path propagation is cxpr; these global steps are host-orchestrated.

The main benefit for this workload is the backend parity contract. Identical
random streams across the reference, generated C, and CUDA backends make runs
reproducible and auditable without coupling results to worker scheduling. Final
numeric results still follow cxpr's floating-point parity contract, especially
when distribution transforms or reductions use transcendental functions or a
backend-dependent reduction tree. A naive shared-state PRNG cannot provide the
stream guarantee; a counter-based RNG keyed per path can.

## Quantum and continuum fields (scope)

This primitive covers quantum computations that reduce to deterministic,
explicit, local stencil evolution: real-time finite-difference evolution of the
Schrodinger, Klein-Gordon, Dirac, or Gross-Pitaevskii equations. A complex
amplitude is carried as two real fields (`re`, `im`) because cxpr values are
real doubles, and global normalization (`<psi|psi> = 1`) is a host reduction
plus a renormalization step.

It does **not** support "all forms of quantum field theory," and this document
does not claim that. The boundary is exactly the primitive's contract —
deterministic, synchronous, local:

- **Lattice QFT Monte Carlo.** Local bosonic Metropolis and heat-bath updates
  become possible with the RNG extension and masked/checkerboard passes. HMC is
  a larger runtime capability requiring reversible trajectory integration,
  global reductions, and global accept/reject. With dynamical fermions it also
  requires nonlocal Dirac-operator solves; these are not local peer stencils.
- **State-vector quantum-circuit simulation** is expressible as a complex
  butterfly with a per-step peer-index table, but it is really global matrix
  application and is better served by dedicated libraries.
- **Tensor networks, DMRG, and exact diagonalization** are global linear
  algebra and out of scope entirely.

Methods that need stochastic sampling, global or nonlocal operators, or an
exponentially large amplitude space fall outside this primitive by construction.

## Suggested implementation phases

1. Define read-only snapshot semantics and typed state-field addressing.
2. Extend model analysis and descriptors with peer-state dependencies.
3. Implement validated CPU/reference execution with two state buffers.
4. Lower fixed peer-slot reads through IR and generated C.
5. Add CUDA lowering with resident topology and state buffers.
6. Add optional convergence reductions.
7. Build topology-neutral fixtures before adding a pathfinding helper library.

Deterministic counter-based randomness (to unlock independent-path Monte Carlo
and local stochastic updates above) is a separate optional track with its own
random-bit parity proof, not one of these phases. HMC and global solver support
remain separate runtime concerns.

Acceptance tests should cover invalid indices, boundary fallbacks, state
isolation, thread partitions, repeated buffer swaps, deterministic ties, and
bitwise or explicitly documented floating-point parity across supported
backends.

## Recommendation

Add indexed read-only snapshot state as a general bulk capability, not as a
pathfinding feature. Keep topology construction and domain policy in the host,
while moving safe peer lookup, double buffering, and backend execution into
cxpr. A separate optional pathfinding library can then provide grid conventions,
convergence, caching, and route extraction on top of this primitive.
