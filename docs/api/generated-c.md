# Generated C deployment

The generated-model interface is the stable boundary between `.cxpr` math and
a native host. The model defines ordered scalar inputs, parameters, stateful
calculations, and outputs. The host owns data acquisition, allocation,
scheduling, persistence, and accelerator dispatch.

The public contract is `<cxpr/generated.h>`. A host should reject an artifact
unless `cxpr_generated_model_descriptor_abi_valid()` succeeds. The current
descriptor ABI is `CXPR_GENERATED_MODEL_ABI_VERSION == 4`; it supports at most
64 inputs, outputs, and parameters. Artifact-owned descriptor names, schema
arrays, and callbacks have static lifetime.

`descriptor->tick(state, inputs, params, outputs)` consumes dense arrays in the
descriptor's declared order. Numbers and booleans cross this ABI as `double`;
boolean values are `0.0` or `1.0`. Defaults in the descriptor are metadata: the
host must construct the actual parameter array.

Allocate `descriptor->state_size()` bytes for each independent evaluator. The
storage must meet normal C alignment requirements and remain alive across ticks.
Before the first tick, zero the entire block or call `descriptor->reset` when it
is present. `selected_ticks[i]` and `selected_state_sizes[i]` allow a host to
deploy only output `i` when the generated artifact supplies that specialization.

Models containing `resample` use the direct generated-function
`cxpr_generated_resample_tick_fn` suffix: ordered `cxpr_resample_view` entries
and a primary cursor follow the normal arguments. Validate every view with
`cxpr_resample_view_validate()`. The view ABI is
`CXPR_RESAMPLE_VIEW_ABI_VERSION == 1`. The host owns `values` and `alignment`;
an alignment entry maps a primary row to a materialized-series row, while
`CXPR_RESAMPLE_ALIGNMENT_MISSING` denotes no value.

The version-4 `cxpr_generated_model_descriptor` stores the scalar four-argument
tick type; it does not carry a resample tick pointer. Use the generated
resample function and the requirement accessors in `<cxpr/model/model.h>`
directly. The current generic `<cxpr/bulk.h>` runner likewise accepts descriptor
ticks only; a CUDA or other resample-aware bulk wrapper is host code.

Generated source is portable C and can be compiled into an application,
library, or build-generated object. CUDA source generation is a separate plugin
surface; `.cxpr` itself does not acquire GPU concepts, allocate device memory,
or launch kernels.

See `<cxpr/model/model.h>`, `<cxpr/generated.h>`,
`tests/generated.test.c`, and `tests/generated_resample_parity.test.c`.

## API inventory

| Public symbol | Role |
| --- | --- |
| `cxpr_generated_model_descriptor_abi_valid` | Validate descriptor version, bounds, names, callbacks, and scalar types. |
| `cxpr_resample_view_validate` / `cxpr_resample_view_valid` | Validate borrowed numeric view buffers. |
| `cxpr_resample_view_status_message` | Convert view validation status to stable diagnostic text. |
| `cxpr_model_compiled_generate_c`, `cxpr_model_compiled_generate_c_outputs` | Generate a full evaluator or a compact selected-output evaluator. |
| `cxpr_model_compiled_generate_c_with_params`, `cxpr_model_compiled_generate_c_specialized` | Generate with baked parameters, optionally selecting outputs. |
| `cxpr_model_compiled_c_param_count`, `cxpr_model_compiled_c_param_name` | Inspect generated parameter order. |
| `cxpr_model_compiled_resample_requirement_count`, `cxpr_model_compiled_resample_requirement_source` | Inspect ordered generated view requirements. |
| `cxpr_model_compiled_resample_requirement_duration_ns`, `cxpr_model_compiled_resample_requirement_interval` | Inspect normalized requirement intervals. |
