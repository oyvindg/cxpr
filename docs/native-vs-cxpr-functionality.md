# Native Primitives vs. CXPR Libraries

## Purpose

CXPR should remain domain-agnostic. Native language and runtime features should
provide only the generic mechanisms required to express deterministic,
bounded, and portable programs. Audio, trading, simulation, and other domain
concepts should be implemented as CXPR libraries or by explicitly configured
hosts.

The default rule is:

> Implement behavior in CXPR when it can be expressed clearly, efficiently,
> and with identical semantics across supported backends. Add native support
> only when correct implementation requires compiler, type-system, storage,
> lifecycle, or backend knowledge.

Frequency of use alone is not a reason to add a builtin.

## Responsibility boundary

Native CXPR is responsible for:

- syntax, types, validation, and diagnostics;
- scalar mathematical and logical primitives;
- arrays, records, indexing, and bounded collection operations;
- deterministic state storage and atomic state commits;
- fixed-size buffers and their memory layout;
- scoped model identity and fixed-size instantiation;
- compile-time bounded iteration and lowering;
- portable backend semantics;
- assertions, parameter constraints, and descriptor metadata.

CXPR libraries are responsible for:

- pure transformations composed from native primitives;
- reusable stateful behavior expressed as scoped models;
- statistics, smoothing, interpolation, and lookup policies;
- domain-specific abstractions and terminology;
- algorithms whose implementation does not require privileged runtime access.

Hosts are responsible for external effects such as files, devices, networks,
event loops, and application-owned data sources.

## Criteria for native functionality

A feature belongs in the native language, compiler, or runtime when one or
more of the following applies:

1. It defines storage layout, ownership, state commit order, or scope identity.
2. It requires static type inference or changes the result type based on its
   arguments.
3. It requires special syntax or compile-time expansion.
4. It operates on collections in a way that cannot use the ordinary scalar
   calling convention.
5. It must guarantee bounded execution time or bounded memory.
6. It requires backend-specific lowering to preserve equivalent C, CUDA, and
   interpreter behavior.
7. It is a fundamental operation that cannot itself be implemented in CXPR.

Native functionality should remain small and orthogonal. Every new primitive
increases the type-checking, IR, code-generation, tooling, documentation, and
backend-parity surface.

## Recommended native primitives

### Scalar operations

Fundamental arithmetic, comparison, boolean, and numerical operations should
remain built in. Examples include `sin`, `cos`, `exp`, `log`, `sqrt`, `pow`,
`floor`, `ceil`, `abs`, `min`, `max`, `clamp`, `isfinite`, and `isnan`.

These require consistent numerical and type semantics across all execution
backends.

### Fixed-size state buffers

The proposed bounded buffer declaration should be completed as a native state
facility:

```cxpr
state observed: buffer<number, samples=64>
observed := source

current = observed[0]
previous = observed[1]
```

The compiler and runtime must define:

- build-time capacity validation;
- allocation-free fixed storage;
- atomic push behavior at the state commit point;
- indexing and startup behavior;
- generated-C layout and backend parity;
- supported element types.

Higher-level operations such as moving averages or RMS calculations should be
implemented in CXPR once buffers and bounded iteration are available.

### Static collections and bounded iteration

CXPR should support generic operations over statically bounded collections,
such as `map`, `reduce`, `any`, `all`, `find`, `count`, `enumerate`, and `zip`.
These may look like functions at the source level, but require native compiler
support for callback typing, result typing, bounds analysis, and unrolling.

Unbounded general-purpose runtime loops should not be required for this use
case. Compile-time-known iteration keeps execution analyzable and generated
code predictable.

### Arrays of homogeneous records

Static event tables and similar data should use ordinary arrays of records:

```cxpr
const entries = [
    {position=0, value=10, length=2},
    {position=4, value=20, length=1}
]
```

CXPR does not need to assign domain meaning to these fields. It needs generic
record type unification, predictable indexing, static lookup operations, and
efficient lowering.

### Fixed-size scoped instantiation

The compiler should support a statically known number of independent model
instances. The exact syntax remains a design decision, but the semantics must
include:

- one stable scope identity per instance;
- separate state per instance;
- deterministic evaluation order;
- compile-time state-size calculation;
- lowering without dynamic allocation.

This mechanism is generic. Libraries may use it for filters, workers,
particles, entities, candidates, or voices without exposing those concepts in
native CXPR.

### Parameter constraints and introspection

Model parameters should be able to publish generic constraints such as type,
minimum, maximum, finiteness, and required/default status. Generated model
descriptors should expose these constraints together with inputs, outputs,
state size, buffer capacities, source locations, and documentation metadata.

This allows CLIs, editors, optimizers, and domain-specific hosts to validate and
present models without duplicating their contracts.

### Generic diagnostics

Compiler and analysis tooling should report issues such as:

- provably invalid or unsafe indexes;
- possible non-finite results where they can be detected statically;
- state read before a defined initial value;
- unused inputs, parameters, state, and outputs;
- dependency or state-update cycles;
- unstable scoped identities;
- unsupported backend operations;
- estimated state, buffer, stack, and operation costs.

Diagnostics should describe generic program behavior rather than assume a
particular application domain.

## Function versus model

Pure behavior should normally be a CXPR function:

```cxpr
fn lerp(a, b, amount) = a + amount * (b - a)

fn remap(value, in_low, in_high, out_low, out_high) =
    lerp(out_low, out_high,
         clamp((value - in_low) / (in_high - in_low), 0, 1))
```

Behavior that owns state should normally be a scoped CXPR model:

```cxpr
model edge(lifecycle="scoped")

in value
state previous = false

rising = value and not previous
falling = not value and previous
changed = value != previous

previous := value

out {rising, falling, changed}
```

The native runtime supplies state and scope semantics; the reusable behavior
remains visible, inspectable, and modifiable CXPR code.

Good standard-library candidates include:

- edge detection;
- hold, latch, toggle, and counters;
- elapsed-since and debounce behavior;
- interpolation and remapping;
- rolling statistics over native buffers;
- smoothing policies;
- generic table lookup and event selection.

These should not become builtins unless a concrete limitation prevents a
correct and efficient CXPR implementation.

## Avoid convenience builtins

Domain operations and easily composable helpers should not be native merely
because they are popular. Examples include musical note conversion, tempo
conversion, oscillators, envelopes, trading indicators, moving averages,
normalization policies, and simulation-specific integrators.

Unnecessary builtins have several costs:

- duplicated implementation and parity work across backends;
- more special cases in parsing, typing, IR, and code generation;
- less inspectable behavior;
- fewer opportunities for users to adapt an algorithm;
- accidental leakage of application concepts into the core language.

If a library implementation reveals a genuine missing primitive, the primitive
should be generalized before it is added to native CXPR.

## Recommended implementation order

1. Complete fixed-size state buffers.
2. Strengthen homogeneous arrays of records and their type checking.
3. Add statically bounded collection transformations with compile-time
   lowering.
4. Add stable fixed-size scoped instantiation.
5. Publish parameter constraints and resource metadata in descriptors.
6. Build generic stateful helpers as a CXPR standard library.
7. Consider nominal types and explicit conversions after the underlying type
   contracts are established.
8. Extend generic resource analysis and diagnostics.

This sequence improves the expressiveness needed by audio examples and other
domains while preserving CXPR as a small, deterministic, domain-agnostic core.

### Implementation status

- [ ] Static array-literal reductions: `sum`, `mean`, `min`, and `max` now lower
  at expression compile time, including pure selectors over homogeneous
  records. The generated expression contains a fixed tree rather than a
  runtime collection loop. Model-import/codegen parity remains the completion
  gate before audio models may adopt the new reducers beyond the existing
  selector-based `sum` path used by the robot-voice sequencer.
- [ ] Generalize the same bounded lowering contract to `map`, `reduce`, `any`,
  `all`, `find`, `count`, `enumerate`, and `zip` after their empty-collection
  and result-type contracts are specified.
