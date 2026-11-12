# Typed bounded views

Typed bounded views let a `.cxpr` model read host-owned numeric memory without
copying it into `cxpr_value` arrays or invoking a callback per element. The
public contracts are `<cxpr/view.h>`, `<cxpr/view_lease.h>`,
`<cxpr/generated.h>`, and `<cxpr/context.h>`.

## Type model

| Type | Responsibility |
| --- | --- |
| `cxpr_view` | Host memory descriptor: data, element type, active shape, byte strides, capacity, and flags. |
| `cxpr_view_requirement` | Generated model contract: stable slot, element type, rank, maximum shape, alignment, and access mode. |
| `cxpr_view_resolved` | Validated binding with an immutable base pointer, active shape, and element strides. |
| `cxpr_view_table` | Ordered resolved views for one published generation. |

`cxpr_view` is not only a shape. Shape is one part of the complete borrowed
memory descriptor. CXPR never owns or frees the referenced storage.

## Choosing an access path

| Requirement | Recommended API |
| --- | --- |
| Host-only C code with no CXPR model | Direct typed pointer access. |
| Generated model indexes contiguous host memory | Resolve a `cxpr_view_table` and call `descriptor->view_tick`. |
| Independent lanes share one immutable data set | `cxpr_bulk_run_views` or `cxpr_bulk_run_range_views`. |
| Tree evaluation or typed-IR parity | `cxpr_context_bind_views`. |
| Backend has runtime-only schema and needs full dynamic checks | `cxpr_view_load_checked` and related helpers. |
| Each lookup needs provider computation or custom index mapping | Engine view-source callback. |
| One scalar is transported per sequential row | Existing generated/session row-view APIs. |

Generated `view_tick` is the normal production path for large contiguous
buffers. Rank and element type are compiled into the artifact, while active
shape remains checked at runtime. Keep provider callbacks when the callback
itself supplies required dynamic semantics.

## Authoring

Declare a maximum shape in the model. Active dimensions supplied by the host
may be smaller:

```cxpr
model image_sample

in image: view<number>[1080, 1920, 4]
in y: int64
in x: int64

fn get_clamped(values, y, x, channel) =
    get(values,
        clamp(y, 0, length(values, 0) - 1),
        clamp(x, 0, length(values, 1) - 1),
        clamp(channel, 0, length(values, 2) - 1),
        0)

pixel = image[y, x, 0]
safe = get(image, y, x, 1, 0.0)
edge = get_clamped(image, y, x, 2)
width = length(image, 1)

out { pixel, safe, edge, width }
```

Version one accepts `number`, `int64`/`int`, and `bool`, with rank one through
four. Dimensions are positive compile-time integers. Normal indexing is
checked, and native `get` returns its typed fallback on an invalid index.
`get_clamped` above is ordinary CXPR code, not a builtin; helpers can compose
`get`, `length`, and `clamp` for the desired rank and fallback type. The
dimension argument to `length` is resolved at compile time.

## Binding a generated model

Generated descriptor ABI 6 stores view requirements separately from scalar
inputs. A view model exposes `descriptor->view_tick`; scalar-only models keep
the unchanged four-argument `descriptor->tick` contract.

```c
#include <cxpr/generated.h>
#include <cxpr/view.h>

double pixels[12] = {0};
cxpr_view binding = {
    .data = pixels,
    .element_type = CXPR_VALUE_NUMBER,
    .rank = 2,
    .shape = {3, 4},
    .strides = {4 * (ptrdiff_t)sizeof(double), (ptrdiff_t)sizeof(double)},
    .capacity_bytes = sizeof(pixels),
    .flags = CXPR_VIEW_READ_ONLY | CXPR_VIEW_CONTIGUOUS,
};
cxpr_view_resolved resolved[CXPR_VIEW_MAX_COUNT] = {0};
size_t failed_slot = SIZE_MAX;

cxpr_view_status status = cxpr_views_resolve(
    descriptor->views, &binding, descriptor->view_count,
    resolved, &failed_slot);
if (status != CXPR_VIEW_OK) {
    fprintf(stderr, "view %zu: %s\n", failed_slot,
            cxpr_view_status_message(status));
    return 1;
}

cxpr_view_table table = {
    .views = resolved,
    .count = descriptor->view_count,
    .generation = 1,
};
descriptor->view_tick(state, scalar_inputs, params, outputs, &table);
```

Bindings match requirements by ordered slot. Resolution validates element
type, rank, maximum shape, row-major byte strides, capacity, alignment, flags,
and overflow-prone products before execution. Version one requires read-only,
contiguous storage. Keep the original storage, resolved entries, and table
alive for every tick using them.

## Evaluator and typed IR

Bind a validated table to authored names with:

```c
cxpr_view_status status = cxpr_context_bind_views(
    context, requirements, &table, requirement_count);
```

Tree evaluation and typed IR use the same checked load semantics as generated
C. Public IR inspection reports `VIEW_LENGTH`, `VIEW_LOAD_CHECKED`, and
`VIEW_LOAD_OR`. Ordinary array indexing and temporal
lookback remain separate operations.

The lower-level `cxpr_view_length`, `cxpr_view_load_checked`,
`cxpr_view_load_or`, and `cxpr_view_load_clamped` functions operate directly on
an already resolved table.

## Generations and real-time containment

`cxpr_view_table.generation` is host-controlled identity. It does not publish
memory atomically. Prepare generation B while A remains active, publish B at an
execution boundary, and reclaim A only after its readers release their leases.

`cxpr_view_lease_acquire` verifies the expected generation and returns
`CXPR_VIEW_STALE_BINDING` when it changed. For real-time callbacks,
`cxpr_view_lease_load_contained` writes the element type's zero value after a
dynamic access failure and records the first error in caller-owned
`cxpr_view_execution_status`. This path allocates and formats nothing.

## Bulk execution

`cxpr_bulk_run_views` and `cxpr_bulk_run_range_views` share one immutable
`cxpr_view_table` across all lanes while scalar columns and state remain
lane-specific. Validate with `cxpr_bulk_validate_views` first.

Imported child models propagate unbound view inputs into the parent schema.
The child session borrows the parent's already validated view table for the
duration of the tick, so composition does not copy the backing storage or
repeat host-side binding. Child models that expose the same inferred input name
must agree on element type, rank, and maximum shape.

The original `cxpr_bulk_run` contract remains scalar-only. It returns
`CXPR_BULK_VIEWS_REQUIRED` for a descriptor declaring model-visible views.

## Compatibility and limitations

- Descriptor ABI 6 is not layout-compatible with ABI 5; rebuild hosts and
  generated artifacts together.
- `cxpr_generated_tick_batch` and `cxpr_model_session_tick_views` still use
  `cxpr_view` for scalar row transport. They do not expose indexable model
  inputs.
- Callback view sources and resample views remain separate contracts.
- Arbitrary strides, writable model-visible views, `f32`, and CUDA residency
  are not part of the current contract.

Executable references are `tests/view.test.c`, `tests/view_eval.test.c`,
`tests/ir_view.test.c`, `tests/view_codegen_e2e.test.c`,
`tests/view_lease.test.c`, `tests/view_concurrency.test.c`,
`tests/bulk_view.test.c`, and the authored trading, image, simulation, and
audio parity fixtures under
`tests/fixtures/external_views/`.

## API inventory

| Public symbol | Role |
| --- | --- |
| `cxpr_views_resolve` | Validate bindings and produce execution-ready views. |
| `cxpr_view_status_message` | Return stable text for a view status. |
| `cxpr_context_bind_views` | Bind requirement names and a table to evaluator/IR execution. |
| `cxpr_view_length` | Read one active dimension. |
| `cxpr_view_load_checked` | Perform a checked typed load. |
| `cxpr_view_load_or` | Perform a checked load with a typed fallback. |
| `cxpr_view_load_clamped` | Clamp integral indices and load. |
| `cxpr_view_lease_acquire` | Pin an expected generation. |
| `cxpr_view_lease_load_contained` | Load with zero containment and first-error status. |
| `cxpr_bulk_validate_views` | Validate shared-view bulk execution. |
| `cxpr_bulk_run_views` / `cxpr_bulk_run_range_views` | Execute lanes against one shared table. |
