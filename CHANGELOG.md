# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - Unreleased

### Added

- IR views and scoped references (`cxpr/ir_view.h`): inspect compiled programs
  without executing them.
- Compiled-program introspection on the expression evaluator:
  `cxpr_expression_program`, `cxpr_expression_instruction_count`,
  `cxpr_expression_dependency_instruction_count`, and
  `cxpr_expression_total_instruction_count`. The dependency variant counts each
  reachable program once; the total variant sums every program independently.
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

[1.1.0]: https://github.com/oyvindg/cxpr/compare/v1.0.4...HEAD
[1.0.4]: https://github.com/oyvindg/cxpr/compare/v1.0.3...v1.0.4
[1.0.3]: https://github.com/oyvindg/cxpr/compare/v1.0.2...v1.0.3
[1.0.2]: https://github.com/oyvindg/cxpr/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/oyvindg/cxpr/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/oyvindg/cxpr/releases/tag/v1.0.0
