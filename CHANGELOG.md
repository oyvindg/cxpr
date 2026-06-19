# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-06-19

### Added

- Provider metadata for host-backed inventories (`cxpr/provider.h`,
  `cxpr/runtime_call.h`, `cxpr/scope.h`): describe host functions and direct
  sources, preserve named arguments, declare record fields, and decode
  scoped/partitioned series; register provider signatures at parse time and turn
  call ASTs into host-neutral runtime-call views.
- Source planning for host-owned data (`cxpr/source_plan.h`):
  `cxpr_plan_bind_sources` and `cxpr_plan_bind_sources_from_table` own the AST
  walk, numeric-argument evaluation, per-leaf host binding, and scoped-source
  registration so hosts materialize series bar-by-bar.
- IR views (`cxpr/ir_view.h`): inspect a compiled program's opcodes, result
  kind, and instruction count without executing it.
- Compiled-program introspection on the expression evaluator:
  `cxpr_expression_program`, `cxpr_expression_instruction_count`,
  `cxpr_expression_dependency_instruction_count`, and
  `cxpr_expression_total_instruction_count`. The dependency variant counts each
  reachable program once; the total variant sums every program independently.
- Alias expansion (`cxpr/alias.h`): `cxpr_expand_aliases` rewrites an expression
  string before parsing.
- Typed context accessors (`cxpr/context.h`): boolean and string variables and
  `$params` (`cxpr_context_set_bool`/`set_string`/`get_bool`/`get_string` and
  the `_param_` variants), plus per-evaluation cached structs
  (`cxpr_context_set_cached_struct`, `cxpr_context_get_cached_struct`,
  `cxpr_context_clear_cached_structs`).
- AST inspection and analysis (`cxpr/ast.h`): `cxpr_ast_clone`,
  `cxpr_ast_to_string`/`cxpr_ast_dump`, producer-field and reference/variable
  collection, call-argument context tracing
  (`cxpr_ast_call_arg_contexts_for_reference`/`_for_variable`),
  `cxpr_ast_is_boolean_expression`, and offset-relative evaluation.
- Registry: `cxpr_registry_add_numeric` for the scalar double fast path (with
  `cxpr_registry_add` kept as a backward-compatible alias) and
  `cxpr_registry_add_ast_handler` to layer AST-level dispatch on an existing
  function without replacing its scalar/struct callbacks.
- Expression language: named call arguments (`close(timeframe="1d")`) preserved
  through the AST/provider path, and string literals as named-argument values.
- `cxpr_error_format` — render a complete `"<code> at <line>:<column>: <message>"`
  description into a caller-owned buffer, with `snprintf`-style truncation
  semantics. Output is self-contained and safe to retain.
- `cxpr_thread_cleanup` — release the calling thread's thread-local overlay
  cache; optional and only affects when memory is reclaimed, never correctness.
- libFuzzer harness (`fuzz/parse_fuzzer.c`, `CXPR_BUILD_FUZZERS` option and
  `fuzz` preset) driving untrusted input through parse → compile → evaluate
  under ASan/UBSan, plus a CI smoke run.

### Changed

- Defined an explicit concurrency contract (see the README "Concurrency"
  section): immutable-after-build handles (`cxpr_registry`, `cxpr_ast`,
  `cxpr_program`) may be shared read-only across threads, while mutable handles
  (`cxpr_context`, `cxpr_evaluator`) must be per-thread.
- The internal empty-overlay reuse cache and the error-message scratch buffers
  are now thread-local, so concurrent evaluation on separate contexts no longer
  races on shared process-global state.
- Documented that `cxpr_error.message` points to `cxpr`-owned storage (a static
  literal or a thread-local scratch buffer) and is only valid until the next
  failing call on the same thread.
- CI now builds and tests with both GCC and Clang.

### Fixed

- Fixed memory leaks on parser error paths: when a sub-parse allocated AST nodes
  and then hit an invalid token, the just-parsed node was leaked while unwinding.
  Affected the binary/unary/ternary/pipe productions and the call-argument,
  parenthesised-group, and lookback paths. Found by the new fuzzer.
- Fixed undefined behavior in the fast scalar and boolean IR executors: the
  stack pointer was read and modified without an intervening sequence point when
  invoking a synchronous function (`stack[sp++] = f(&stack[sp], ...)`), which
  could pass the wrong argument pointer. Split into ordered statements.
- The `strict` preset (`-Wall -Wextra -Wpedantic -Werror`) now builds
  warning-clean on GCC and Clang; the computed-goto IR executor scopes its
  `-Wpedantic` suppression to `fast.c` only.
- `cxpr_error_string(CXPR_ERR_TYPE_MISMATCH)` now returns `"Type mismatch"`
  instead of falling through to `"Unknown error"`.

## [1.0.4] - 2026-04-16

## [1.0.3] - 2026-04-05

## [1.0.2] - 2026-04-05

## [1.0.1] - 2026-04-05

## [1.0.0] - 2026-04-04

Initial tagged releases. Detailed per-version history predates this changelog;
see the Git tags `v1.0.0`–`v1.0.4` for the corresponding commits.

[1.1.0]: https://github.com/oyvindg/cxpr/compare/v1.0.4...v1.1.0
[1.0.4]: https://github.com/oyvindg/cxpr/compare/v1.0.3...v1.0.4
[1.0.3]: https://github.com/oyvindg/cxpr/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/oyvindg/cxpr/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/oyvindg/cxpr/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/oyvindg/cxpr/releases/tag/v1.0.0
