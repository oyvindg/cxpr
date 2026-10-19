# int64 host ABI migration

The int64 work is additive for existing C and managed hosts. Existing double
callbacks, context functions, history descriptors, resample views, and generated
model entry points keep their layouts and signatures.

## Managed bindings

Bind C `int64_t` as the managed signed 64-bit type (`long` in C#). Bind C
`bool*` outputs as one-byte booleans. The exact context symbols are:

- `cxpr_context_set_int64` / `cxpr_context_get_int64`
- `cxpr_context_set_param_int64` / `cxpr_context_get_param_int64`

Do not route integer identifiers through the legacy double setters when values
may exceed `2^53`.

## ABI policy

- New typed functions and structs are added beside legacy APIs.
- Existing public struct layouts are not extended in place.
- Additive symbol exports do not require a shared-library major-version bump.
- A changed existing signature, enum representation, or public struct layout
  requires the next major ABI version.
- `cxpr_resample_view` remains the number-only v1 layout. Generated models with
  int64 resample requirements use `cxpr_typed_resample_view` and its parallel
  generated tick signature.

The shared-library smoke tests resolve the four int64 context symbols at runtime
and the .NET test verifies exact round trips above `2^53`.
