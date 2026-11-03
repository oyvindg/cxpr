# Generic views in CXPR

Status: borrowed rank-one host views and static collections implemented;
native multidimensional view ports remain planned

## Purpose

A view is a borrowed, typed description of existing contiguous or strided
memory. It lets one CXPR model evaluation consume or produce many related
values without copying every element into a separate scalar input.

Views are a data representation, not a streaming API. CXPR remains unaware of
files, sockets, codecs, devices, clocks, backpressure, and media formats. A
host may repeatedly evaluate the same stateful model with new views to build a
real-time or offline pipeline, but the scheduling policy remains outside
CXPR.

The design should preserve one execution path:

```text
host-owned data -> cxpr_value inputs -> generated tick -> cxpr_value outputs
```

There must not be separate audio, video, trading, or stream evaluators.

## Problems views solve

### Efficient block processing

Passing one scalar at a time requires one ABI call and repeated input binding
for every sample, bar, pixel, or event. A view binds a complete block once:

```text
scalar audio at 44.1 kHz: 44,100 tick calls per second
256-frame audio views:       about 173 tick calls per second
```

The same reduction applies to historical bars, indicator input series, image
planes, and event collections. Contiguous views also improve cache locality
and make SIMD, bulk execution, CUDA upload, and zero-copy host integration
practical.

### Mixed scalar and block inputs

A model must be able to combine views with ordinary controls:

```cxpr
model filter

in samples: view<number, shape = [*, 2]>
in cutoff: number
in bypass: bool
in timestamp_ns: int64

out samples: view<number, shape = [*, 2]>
out peak: number
```

`samples` is block data, while `cutoff`, `bypass`, and `timestamp_ns` remain
normal values. The model is not globally classified as either scalar or
streaming; each port declares its own value shape.

### Ordered event collections

Views provide the missing storage and indexing foundation for
`tests/fixtures/events/timed_event_cursor.cxpr`. The event fields can use a
struct-of-arrays layout:

```cxpr
in event_at: view<number, shape = [*]>
in event_value: view<number, shape = [*]>
in event_weight: view<number, shape = [*]>
in event_channel: view<int64, shape = [*]>
in position: number

cursor = upper_bound(event_at, position) - 1
index = clamp(cursor, 0, length(event_at) - 1)
value = event_value[index]
weight = event_weight[index]
channel = event_channel[index]
```

The view itself does not implement cursor semantics. Generic operations still
needed are `length(view)`, checked dynamic indexing, and
`upper_bound(view<number>, value)`. Trigger, age, and previous-cursor state
remain ordinary model logic.

### Trading data

A trading bar is a domain record; a view is its physical data representation.
For indicator workloads, separate field views are usually preferable:

```cxpr
in timestamp: view<int64, shape = [*]>
in open: view<number, shape = [*]>
in high: view<number, shape = [*]>
in low: view<number, shape = [*]>
in close: view<number, shape = [*]>
in volume: view<number, shape = [*]>
in period: int64
```

This struct-of-arrays layout lets an indicator read only the fields it needs,
keeps each series contiguous, and maps naturally to existing CPU and CUDA
execution. A future logical `view<bar>` may lower to the same physical layout;
it must not introduce a competing storage system.

For one live bar, scalar inputs may remain cheaper and simpler. Views are most
valuable for blocks, historical evaluation, and crossing language or device
boundaries.

### Real-time media filtering

The same representation can carry audio blocks or video planes:

```cxpr
# Interleaved stereo audio.
in samples: view<number, shape = [*, 2]>

# One RGB video frame.
in pixels: view<number, shape = [*, *, 3]>
```

A media host owns decoding, buffer pools, timing, and output. It binds an input
view, pre-binds an output view, and calls the existing generated tick. CXPR
only performs the deterministic transformation.

## Proposed native representation

The representation must be generic, borrowed, and explicit about layout:

```c
#define CXPR_VIEW_MAX_RANK 4u

typedef struct cxpr_view {
    void *data;
    cxpr_value_type element_type;
    size_t rank;
    size_t shape[CXPR_VIEW_MAX_RANK];
    ptrdiff_t strides[CXPR_VIEW_MAX_RANK];
    size_t capacity_bytes;
    unsigned flags;
} cxpr_view;
```

`cxpr_value` has a view variant alongside `number`, `bool`, and `int64`.
Views borrow their memory: CXPR neither allocates, retains, nor frees the
buffer.

The flags should be limited to layout and ownership-independent properties,
for example read-only, writable, contiguous, and safe input/output aliasing.
They must not include media concepts such as channels, sample rate, pixel
format, or frames per second.

## One tick contract

The existing generated tick remains the only evaluator:

```c
typedef void (*cxpr_generated_tick_fn)(
    void *state,
    const cxpr_value *inputs,
    const cxpr_value *params,
    cxpr_value *outputs);
```

The host binds both scalar values and views through the same arrays:

```c
inputs[samples_slot] = cxpr_view_value(&input_block);
inputs[gain_slot] = cxpr_num(0.8);

/* The host supplies writable output storage before evaluation. */
outputs[samples_slot] = cxpr_view_value(&output_block);

descriptor->tick(state, inputs, params, outputs);
```

Pre-bound output memory is required for deterministic real-time behavior. A
generated tick must not allocate, resize, or free a view. Scalar outputs are
written into the same output array as today.

State continuity also remains unchanged. One state block is allocated and
reset before processing begins, then reused for every evaluation. Independent
pipelines use independent state blocks; concurrent calls must not share mutable
model state.

## Descriptor metadata and validation

The generated descriptor should describe each port independently:

```c
typedef struct cxpr_generated_port_descriptor {
    const char *name;
    cxpr_value_type value_type;
    cxpr_value_type element_type;
    size_t rank;
    int64_t shape[CXPR_VIEW_MAX_RANK];
    unsigned flags;
} cxpr_generated_port_descriptor;
```

`-1` in a declared shape represents a runtime dimension. For example,
`[-1, 2]` accepts any positive block length but requires two elements in the
second dimension.

Hosts should validate bindings before entering a latency-sensitive loop. The
validation checks:

- value and element types;
- rank and fixed dimensions;
- non-null data for non-empty views;
- strides and required capacity;
- writability of pre-bound outputs;
- permitted input/output aliasing;
- equal lengths for ports linked by model constraints.

Binding validation is not another evaluator. After successful validation, the
host continues to call the same generated tick.

## Language operations

The compile-time subset already supports array literals and records whose
fields are equally sized arrays. Such a record is a static row view: indexing
it selects the same checked index from every field, so fields may retain
different scalar types without an array-of-union representation.

```cxpr
events = {
    at = [0.0, 0.5, 1.5],
    note = [37, 56, 61],
    active = [true, true, false]
}

cursor = upper_bound(events.at, position) - 1
current = events[clamp(cursor, 0, length(events) - 1)]
```

`length` is folded at compile time. `upper_bound` accepts a sorted numeric
array and returns the first index whose element is greater than the search
value. Dynamic indexing checks that the index is finite, integral, and within
bounds; generated numeric expressions produce `NAN` for an invalid index.
The optional `upper_bound(records, value, row => row.key)` form supports
static arrays of records.

This subset does not change the host ABI and does not schedule iteration. It
is static model data lowered by the compiler. Runtime-borrowed `cxpr_view`
values described below are still future work.

Static callbacks may use an expression block when intermediate values improve
clarity:

```cxpr
map(events, event => {
    cursor = upper_bound(event.at, position) - 1;
    index = clamp(cursor, 0, length(event.at) - 1);
    return event.value[index];
})
```

Bindings are lexical, ordered, and immutable. The block is compile-time
expression syntax: it does not introduce a host loop, mutable state, or an
audio-specific scheduling concept.

The first runtime implementation binds rank-one views to existing scalar model
ports with `cxpr_model_session_tick_views`. The first dimension is processed
in deterministic order, preserving atomic state commits between elements. This
provides an allocation-free C hot loop and one host/native transition per
block without introducing a second evaluator. Native `view<T, shape=...>`
ports still need the following deliberately small operation set:

- `length(view)` and shape inspection;
- checked dynamic indexing;
- sequential iteration over a declared dimension;
- element-wise scalar expressions;
- reductions such as `min`, `max`, and `sum`;
- ordered search such as `lower_bound` and `upper_bound`;
- explicit slicing that returns another borrowed view.

Stateful DSP and indicators must process their time dimension in order. View
syntax must not imply that a compiler may parallelize dependent samples or
bars. Independent channels, strategies, instruments, or image regions may be
parallelized when analysis proves it safe.

Compile-time array literals may be lowered to read-only static views. This
would allow the timed-event fixture to keep its events in the model while
using the same indexing and search operations as host-provided event views.

## Relationship to existing resample views

`cxpr_resample_view` and `cxpr_typed_resample_view` already borrow buffers for
pre-materialized temporal series. A generic view must replace their duplicated
buffer description rather than create a third parallel representation.

The target layering is:

```text
cxpr_view
  generic typed memory, shape, and strides

cxpr_aligned_view
  cxpr_view values
  cxpr_view alignment
  primary-count and resampling metadata
```

Resampling and alignment remain domain-specific metadata over generic views.
Existing ABI layouts may require a compatibility adapter during migration, but
new generated code should converge on one view representation.

Likewise, `tickNumericBatch` represents repeated scalar evaluations. A view
represents one evaluation receiving structured block data. Both use the same
model session and state; neither requires a separate execution engine.

## Real-time requirements

Views make real-time processing possible but do not provide scheduling. A
real-time host is responsible for deadlines, pacing, queues, and underrun
policy. CXPR's part of the contract is narrower:

- compilation occurs before processing starts;
- model state and buffers are allocated before the first tick;
- tick performs no allocation, I/O, locking, or lazy initialization;
- view bounds and shapes can be validated outside the hot loop;
- generated processing has deterministic ownership and state behavior;
- diagnostics never write into data buffers.

Block size remains a host decision. Smaller blocks reduce latency; larger
blocks reduce call overhead and often improve throughput.

## Non-goals

Generic views do not add any of the following to `libs/cxpr`:

- a stream runner or pipeline framework;
- audio or video APIs;
- codecs, file formats, devices, or network transports;
- backpressure, queues, scheduling, or real-time clocks;
- VLC, FFmpeg, camera, or market-provider integration;
- automatic ownership of host buffers.

Those concerns belong to hosts and adapters. CXPR supplies one typed evaluation
mechanism that those systems can call.

## Recommended implementation sequence

1. [x] Add a borrowed primitive `cxpr_view` variant to `cxpr_value`.
2. [x] Add rank-one scalar-port binding validation and ordered block execution.
3. [ ] Add native view-port descriptor metadata and standalone binding validation.
4. [ ] Support native read-only view inputs, `length`, and checked dynamic indexing.
5. [x] Support host-pre-bound writable rank-one output views.
6. [x] Add compile-time read-only views for array literals.
7. [x] Implement ordered search and migrate `timed_event_cursor.cxpr`.
8. [ ] Generalize resample storage to use `cxpr_view` behind a compatibility layer.
9. [ ] Add native multidimensional sequential view lowering.
10. [ ] Verify native view ports in Node, CPU bulk, and CUDA against the same descriptor
   contract before adding richer record or tensor syntax.

This sequence delivers immediate value for events and trading series while
keeping the ABI suitable for later real-time audio and video hosts.
