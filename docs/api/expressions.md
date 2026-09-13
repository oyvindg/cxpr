# CXPR expressions API

This page covers parsing, analysis, compilation, and direct evaluation. See
[Core](core.md) for contexts, values, registries, and ownership, and
[Execution](execution.md) for reusable sessions and generated artifacts.

## Parsing and AST ownership

`<cxpr/parser.h>` owns the parse lifecycle:

```c
cxpr_error err = {0};
cxpr_expr_parser *parser = cxpr_expr_parser_new();
cxpr_expr_ast *ast = cxpr_expr_ast_parse(parser, "x * 2 + $bias", &err);

cxpr_expr_ast_free(ast);
cxpr_expr_parser_free(parser);
```

The returned AST is independently owned. `<cxpr/expr/ast.h>` provides node
inspection, construction, cloning, serialization, and reference/call discovery.
Prefer accessors such as `cxpr_expr_ast_kind_of` and the node-specific child
accessors. Constructors document whether they take ownership of children.
`CXPR_NODE_LOOKBACK` is a deprecated compatibility alias for
`CXPR_NODE_INDEX`; use index terminology in new code. `<cxpr/token.h>` is a
low-level lexer/token API.

The parser supports typed literals, identifiers, `$parameters`, arrays, records,
field and chain access, positional or named calls, unary/binary operators,
ternaries, and indexing. Valid function and producer names come from the
registry; CXPR core does not embed domain-specific physics, finance, grid, or
device concepts.

## Direct evaluation

`<cxpr/eval.h>` evaluates an AST against borrowed `cxpr_context` and
`cxpr_registry` objects. `cxpr_eval_ast` produces a typed value;
`cxpr_eval_ast_number` and `cxpr_eval_ast_bool` enforce scalar result types.
The lookback and `_at_offset` variants are low-level host/history entrypoints.

Check the boolean return and then inspect `cxpr_error`; do not infer failure
from zero or `NAN`. The AST, context, and registry must remain alive for the
call. The direct-evaluation API does not declare an independently owned lifetime
for nested result payloads. If a typed result must outlive its context,
registry callbacks, or the next evaluation, retain an explicit
`cxpr_value_clone` and later call `cxpr_value_free` on that clone.

Direct evaluation suits tooling and one-shot interpretation. Use an evaluator
or engine session for repeated execution.

## Named expression graphs

`<cxpr/evaluator.h>` and `<cxpr/expression.h>` provide this lifecycle:

1. `cxpr_evaluator_new(registry)` creates an evaluator borrowing the registry.
2. `cxpr_expression_add` or `cxpr_expressions_add` adds named
   `cxpr_expression_def` entries.
3. `cxpr_expression_compile` resolves dependencies and detects errors.
4. `cxpr_expression_eval_all` evaluates the graph against a context.
5. `cxpr_expression_get`, `_get_double`, or `_get_bool` retrieves results.
6. `cxpr_evaluator_free` releases the evaluator.

`cxpr_analyze_expressions` analyzes a graph without executing it. Evaluation
order and instruction-count functions support diagnostics. The pointer returned
by `cxpr_expression_program` is a low-level borrowed program owned by the
evaluator.

`cxpr_expression_inline_params` returns an allocated string; the caller frees
it with `free` as specified by `<cxpr/expression.h>`.

## Compilation and typing

`<cxpr/expr/compiled.h>` and `<cxpr/ir.h>` expose lower-level compiled-expression
and reference-IR APIs for compiler tooling, inspection, and parity validation.
Application execution should normally use [engine sessions](execution.md).

`<cxpr/typecheck.h>` provides explicit type validation. Type-check with the same
registry used for execution because registered signatures form part of the type
environment.

## Errors and threading

Failures use `cxpr_error`. Its message is borrowed and may be overwritten by the
next failing CXPR call on the same thread; use `cxpr_error_format` to retain it.
Parsers, contexts, registries, and evaluators are mutable and should not be used
concurrently unless the owning header explicitly says otherwise. A common
design is one parser/context/evaluator per worker.
