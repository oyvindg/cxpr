# CXPR language reference

This document describes the language accepted by the current cxpr lexer,
expression parser, document/model parser, and model validator. It is derived
from those implementations and their positive and negative tests; older
Markdown descriptions are not normative.

## Source of truth

When sources disagree, executable code and tests take precedence in this
order:

| Area | Implementation | Contract tests |
|---|---|---|
| Tokens, literals, comments, aliases | `src/lexer/lexer.c` | `tests/lexer.test.c`, `tests/errors.test.c` |
| Expression grammar and precedence | `src/parser/expression.c`, `src/parser/primary.c`, `src/parser/helpers.c` | `tests/parser.test.c`, `tests/parser_expression.test.c`, `tests/parser_primary.test.c`, `tests/precedence.test.c` |
| Arrays, indexing, history | `src/parser/primary.c`, `src/model/lookback/collect.c` | `tests/array_index.test.c`, `tests/history_index.test.c`, `tests/invalid_index_fixtures.test.c`, `tests/fixtures/index/*` |
| Document and model declarations | `src/ast/document/parser.c`, `src/ast/document/accessors.c`, `src/model/parse.c` | `tests/document.test.c`, `tests/model.test.c`, `tests/model_header.test.c` |
| Symbols and validation | `src/model/validate.c` | `tests/model.test.c`, `tests/model_imports.test.c` |
| Resampling/timeframes | `src/resample.c`, `src/model/validate.c` | `tests/resample.test.c`, `tests/timeframe_fixture.test.c`, `tests/fixtures/timeframes/*` |

Parsing an expression and compiling or evaluating it are separate operations.
Likewise, `cxpr_model_parse` builds a model, while `cxpr_model_validate` and
compilation impose symbol, call, type, index, source, and host-specific
constraints. A form listed as syntactically valid may therefore still fail in
validation or execution when its names or operand types are invalid.

## Lexical structure

Whitespace separates tokens and is otherwise ignored. Three comment forms are
accepted:

```cxpr
# to end of line
// to end of line
/* across lines */
```

Block comments do not nest. In document parsing, comment text is replaced
before statements are split, while quoted strings are preserved.

Identifiers match `[A-Za-z_][A-Za-z0-9_]*`. Names are case-sensitive. This is
also the rule for model symbols, function parameters, record field names, and
ordinary import path segments. Parameter references begin with `$`, for
example `$period` and `$risk.stop`; each dotted segment obeys the identifier
rule. The `$` is not part of the stored parameter name.

Numbers are decimal integer, fractional, or scientific literals. A leading
zero before the decimal point is optional:

```cxpr
0  42  3.14  .5  .25e2  1e3  1.5e-3  2E+4
```

A leading-dot literal such as `.5` is exactly an alias for `0.5`; it is not
field access because a field-access dot follows an expression. A leading sign
is a unary operator. `null`, `Null`, and `NULL` lex as the numeric NaN sentinel.
No other capitalization of `null` is special.

Strings use single or double quotes. A backslash causes the following
character to be skipped while finding the closing quote, but the lexer retains
the source bytes rather than interpreting C-style escapes:

```cxpr
"1h"  'EU'
```

Boolean literals are `true`, `TRUE`, `false`, and `FALSE`. Mixed-case variants
are ordinary identifiers. Keyword aliases are similarly exactly lowercase or
uppercase, not generally case-insensitive.

Semicolons are accepted only by the document layer as optional trailing
statement punctuation. The expression lexer itself has no semicolon token.

## Expressions

### Primary forms

```ebnf
primary     = number | boolean | string | parameter | identifier
            | call | access | "(" expression ")"
            | array | record ;
array       = "[" [ expression { "," expression } ] "]" ;
record      = "{" [ field { "," field } ] "}" ;
field       = identifier [ ("=" | ":") expression ] ;
call        = qualified-name "(" [ argument { "," argument } ] ")" ;
argument    = [ identifier "=" ] expression ;
```

Record shorthand `{x, y}` is equivalent to `{x = x, y = y}`. Empty arrays and
records are valid. A trailing comma is not accepted. Calls may use positional
or named arguments; argument-name and arity checks belong to function
registration/compilation rather than lexical parsing.

Access forms are:

```cxpr
point.x
outer.inner.value
(choose ? left : right).value
math.clamp(x, 0, 1)
macd(close, fast=12, slow=26).signal
```

`a.b` is field access and deeper identifier paths are chain access. A dotted
qualified name followed by `(...)` is a function name. A call followed by one
`.field` is producer-field access. Parenthesized expressions may also be
followed by one field access. Index suffixes may repeat and apply to any parsed
primary, for example `matrix[1][0]` or `sample(close).value[1]`.

### Operators and precedence

From lowest to highest precedence:

| Level | Forms | Associativity/notes |
|---|---|---|
| Pipeline | `|>` | left; injects the left value as the first argument of a callable right stage |
| Conditional | `condition ? yes : no` | branches are full expressions |
| Logical OR | `or`, `OR`, `||` | left |
| Logical AND | `and`, `AND`, `&&` | left |
| Logical NOT | `not`, `NOT`, `!` | prefix, recursive |
| Equality | `==`, `eq`, `EQ`; `!=`, `ne`, `neq` and uppercase aliases | one equality operator per grammar level |
| Relational/membership | `<`, `>`, `<=`, `>=`, aliases; `in`, `not in` | relational chains supported; membership requires a non-empty array literal |
| Additive | `+`, `-` | left |
| Multiplicative | `*`, `/`, `%` | left |
| Unary sign | `+`, `-` | prefix |
| Power | `^`, `**` | right associative |
| Primary/postfix | calls, `.`, `[]`, grouping | highest |

Comparison aliases are `lt`, `gt`, `le`, `lte`, `ge`, `gte` and their fully
uppercase forms. Equality does not use single `=`; that token is reserved for
named arguments and declarations.

Power binds more tightly than unary minus: `-2^2` is `-(2^2)`, and
`2^-2^2` is `2^(-(2^2))`. `not` binds less tightly than comparisons, so
`not a > b` means `not (a > b)`. Arithmetic binds before comparisons, `and`
before `or`, and the pipeline is the outermost operator.

Relational chains are lowered to conjunctions. Ordinary chains compare
adjacent operands (`1 < x < 10`). The parser also preserves the intended
subject for crossed-bound forms such as `x > 1 < 10`; prefer conventional
`1 < x and x < 10` when clarity matters.

Membership is sugar for `contains`:

```cxpr
region in ["US", "EU"]
x not in [10, 20, $limit]
```

The right side must begin with `[` and contain at least one element. `x in []`
and the removed `x within [a, b]` infix form are syntax errors; use
`within(x, a, b)` for interval testing.

A pipeline stage must be callable. These are equivalent:

```cxpr
x |> normalize |> clamp(0, 1)
clamp(normalize(x), 0, 1)
```

The piped expression is inserted as argument zero. Bare identifiers, ordinary
calls, qualified calls, and producer accesses are handled as callable stages;
an arbitrary value on the right is rejected.

### Values and runtime typing

The expression model contains numbers, booleans, strings, arrays, records
(struct values), and null/NaN. Operators and registered functions enforce their
accepted types during compilation or evaluation. Field access requires a
record/struct at every intermediate segment. Indexing an array returns its
element value; invalid container types and invalid fields are errors rather
than implicit conversions.

Compilation rejects every mismatch that can be proven from literals, operator
results, array elements, record fields, and registered function signatures.
Values supplied by a host or another model can remain statically unknown; the
same checks are then enforced when the expression is evaluated. Unknown is not
a runtime value type and does not permit a known incompatible value to pass.

Declarations accept `: number`, `: bool`, `: int`, `series<T>`, and
`buffer<T, samples=N>`. The executable fixtures
`tests/fixtures/syntax/typed_model_declarations*.cxpr` are parsed, validated,
type-checked, and compiled by the model test suite.

The accepted design contract for the future buffer declaration is:

```cxpr
state observed: buffer<number, samples = 64>
observed := source
```

`samples` is a positive build-time integer and the maximum capacity. A buffer
starts empty and a buffer declaration must not have an `=` initializer. The
right side of `:=` is evaluated once alongside the tick's other expressions;
at the existing atomic state-commit point, scalar `:=` replaces its value while
buffer `:=` pushes one value. A push to a full buffer removes the oldest value.
`observed[0]` is the newest committed sample and `observed[1]` the previous
sample. Expressions evaluated during a tick see only the buffer state committed
by earlier ticks.

## Indexing and temporal history

The syntax `target[index]` is shared by arrays and temporal lookbacks. Meaning
is resolved from the target during compilation/binding:

```cxpr
[10, 20, 30][1]       # array element
close[0]              # current sample
close[1]              # previous sample
(close + high)[2]     # historical compound expression
close[offset]         # dynamic index where supported
```

Indexes must resolve to finite, non-negative integers within the supported
range. Fractional, NaN/non-finite, overflowing, negative, and future-history
indexes are rejected. An empty index (`close[]`) is a syntax error. Bounds
behavior for a valid dynamic index depends on the bound array/history source
and host policy.

Lookbacks are relative to the materialized series being indexed. Thus
`resample(close, "1h")[1]` means the previous hourly sample, not the previous
host tick.

## Model documents

A model file is a sequence of top-level statements. A statement normally ends
at the next non-indented top-level line; braces allow multiline blocks, and
indented lines continue the current statement. Commas or newlines separate
entries in declaration and host blocks. Optional trailing semicolons are
trimmed.

A representative model is:

```cxpr
model crossover { kind = "signal" }
use ema from indicators
in { close, position }
${ fast = 12, slow = 26 }

fast_line = ema(close, period=$fast)
slow_line = ema(close, period=$slow)
state bars = 0
bars := bars + 1

out signal = cross_above(fast_line, slow_line) and not position
out bars
```

### Model declaration and metadata

```cxpr
model name
model name { key = value, flag, nested { key = value } }
```

A non-empty model name is required by validation. The braced body is model
metadata represented as a host block. Bare host fields mean `true`. Host block
syntax uses `=`, not YAML `:` mappings.

Metadata values are stored as source text and can be strings, numbers, arrays,
flags, or nested blocks. A newline or comma must separate adjacent assignments;
`a = 1 b = 2` is rejected as a nested assignment in one value.

### Imports

Accepted forms include:

```cxpr
use indicators/ema
use robotics as r
use ema from indicators
use asx, macd, rsi from indicators
use { atr, ema, supertrend } from indicators
```

`use` introduces a namespace; imported functions are called through that
namespace. `as` selects the namespace explicitly:

```cxpr
# geometry2d.cxpr
fn vec2(x, y) = x, y
fn length(v) = sqrt(v.x * v.x + v.y * v.y)

# simulation.cxpr
model simulation
use geometry2d as g

in { position_x, position_y }
position = g.vec2(position_x, position_y)
out distance = g.length(position)
```

Without `as`, the effective namespace is the imported model/import leaf name.
For example, `use math_helpers` exposes `math_helpers.twice(value)`. The
`use member from package` and braced forms are path conveniences for importing
one or several package members; calls still use each effective imported
namespace rather than becoming unqualified global functions.

Paths are relative, slash-separated identifier segments; an optional final
`.cxpr` suffix is recognized. Absolute paths, empty segments, and invalid
identifier segments are rejected. `from` prefixes every listed member.
Validation rejects duplicate import paths and duplicate effective namespaces
(the alias when present, otherwise the imported namespace). Resolving whether
a target exists is a separate host/import-resolution step. The host callback
receives the importer identity and exact `use` path and returns a canonical
identity plus source text. cxpr detects cycles and duplicate effective
namespaces while building the import bundle.

### Inputs and parameters

Inputs may be singular, grouped, or rooted records:

```cxpr
in close
in { close, high, low }
in market { open, high, low, close }
```

The rooted form declares accessible names such as `market.close`. A defaulted
input is a parameter and must use `$name = expression`:

```cxpr
in { close, $period = 14 }
$period = 14
${ fast = 12, slow = 26 }
$ { threshold = 0.5 }
```

Parameter declaration blocks accept comma- or newline-separated assignments.
Parameter references always use `$period`; ordinary inputs and bindings use
their unprefixed names. A declaration can carry metadata:

```cxpr
$period = 20 { min = 1, max = 100, optimize = [8, 13, 21] }
```

Constant/parameter initializers may refer to previously declared parameters,
but validation rejects unknown `$` names and ordinary runtime symbols in a
constant expression.

### Bindings

An unprefixed assignment creates an immutable expression binding:

```cxpr
mid = (high + low) / 2
signal = fast > slow
```

The left side must be one identifier. A binding may refer to inputs, constants
through `$name`, imported/external references, and other model bindings as
allowed by validation. A binding may not duplicate an input, and duplicate
bindings are rejected.

Record expressions can refer to fields declared earlier in the same record;
field validation proceeds in source order. Forward references to later record
fields are not in scope.

### State and updates

Persistent state is declared with `state` and updated with `:=`:

```cxpr
state count = 0
state { total = 0, ready = false }

count := count + 1
total := total + sample
```

The compact form declares the initializer and update together:

```cxpr
bars := bars + 1 initial 0
```

Both expressions are required. Each update must name an existing state (or the
state created by the compact form), and only one update per state is accepted.
The initial declaration and its update intentionally share a name; other
duplicate bindings do not. `out state := expression` is also parsed as a state
update while making the state public. The obsolete `out name = name + 1` form
does not mean an update.

The typed-buffer form deliberately has no initializer and uses the
same staged-update syntax:

```cxpr
state observed: buffer<number, samples = 64>
observed := sample
```

For buffer state, commit pushes instead of replacing.
The same buffer semantics are portable across model sessions, generated C,
CPU bulk execution, and CUDA device emission.

### Outputs

Outputs may expose existing public symbols or define expressions:

```cxpr
out signal
out { signal, score }
out signal = fast > slow
out signal as entry
out signal { label = "Signal", plot { color = "#22c55e" } }
out signal as entry { label = "Signal" }
```

`as role` becomes output metadata. It cannot be combined with another `role`
field in the same metadata block. Output names must be identifiers. Validation
rejects duplicate outputs and outputs referring to unknown or local-only
symbols.

A call expression without `=` is accepted as an anonymous output:

```cxpr
out publish(signal)
```

### Functions

Scalar expression functions use:

```cxpr
fn clamp_period(period) = max(1, period)
```

Block functions contain local assignments followed by `return` (or `out` as a
return synonym):

```cxpr
fn gap_risk(close, reference, peak) {
    denominator = max(peak, close * 0.001)
    fade = (peak - reference) / denominator
    return denominator > 0 and fade > 0.2
}
```

A bare `return` may continue on subsequent indented lines. Record-return forms
use a record literal or comma-separated return fields. Function parameters and
earlier local/record fields form the local scope. Unknown references and
duplicate function names (including collisions between scalar and record
functions) are rejected.

### Host blocks

Any non-reserved top-level identifier can introduce an opaque host block:

```cxpr
execution backtest {
    initial_cash = 100000
    trace
    fees { percent = 0.1 }
}
```

The block kind is an identifier. Its optional instance name may additionally
contain `-` after the first alphanumeric/underscore character. Fields use
`name = source-text`, a bare `name` flag, or nested blocks. The cxpr core stores
and exposes these blocks; registered host-block validators define the meaning
of particular kinds and fields.

`name`, `model`, `use`, `in`, `fn`, `update`, `out`, `state`, and `meta` are
reserved as host-block kinds. In particular, legacy `meta name { ... }` blocks
are rejected; attach metadata directly to the model, parameter, or output.

## Resampling

The language-level form is a normal call with a constrained contract:

```cxpr
hourly = resample(close, "1h")
hourly = resample(close, every="1h")
previous_hour = resample(close, "1h")[1]
```

The source must be a source/reference expression suitable for host binding,
not an arbitrary scalar. The interval must be a build-time string literal.
Accepted fixed-duration suffixes are exactly `ns`, `us`, `ms`, `s`, `m`, `h`,
and `d`. The magnitude must be a positive
integer that does not overflow. Zero, negative, fractional/dynamic intervals,
unknown units, and calendar intervals are rejected. `resample` selects a
provider-backed materialized series; it is not scalar aggregation performed by
the expression evaluator.

Window reductions such as `mean(window(value, period))`, `min`, `max`, `sum`,
`stddev`, `wma`, `roc`, and `mean_absdev` have parser lowering for the supported
window shapes. Whether a function is callable still depends on the registry and
compiler configuration.

## Validation summary

For a complete model, current validation additionally enforces:

- a model name;
- unique imports/effective import namespaces, inputs, outputs, constants,
  functions, and bindings;
- no binding/input collision;
- exactly the allowed state declaration/update name pairing;
- output names that resolve to public inputs, expression bindings, states, or
  state updates (not local-only bindings);
- `$parameter` references that resolve to declared constants/parameters;
- ordinary references that resolve in the applicable model, function, record,
  import, or host-provided external scope;
- valid `resample` contracts in constants, bindings, and function fields.

The model validator does not by itself prove that every called builtin exists.
Function registration, model compilation, source binding, and host-block
validation add their own errors after parsing/model validation.

## VS Code and VSIX editor tooling

The repository contains a separate VS Code extension in
`tools/vscode-cxpr`. It is editor tooling, not part of the CXPR runtime, C API,
language parser, or generated-artifact ABI. Its manifest requires VS Code
`^1.85.0`, registers `.cxpr` files as language id `cxpr`, and activates on that
language.

The extension provides two complementary layers:

- `syntaxes/cxpr.tmLanguage.json` supplies TextMate highlighting for comments,
  strings, parameters, document fields, keywords, function-like calls,
  properties, numbers, operators, and host-block labels.
- `syntaxes/cxpr.language-configuration.json` configures `#` line comments,
  `/* ... */` block comments, brackets, auto-closing pairs, and surrounding
  pairs.
- `extension.js` starts the bundled CXPR language server over IPC, watches
  `*.cxpr` files, and applies the extension's semantic-token color defaults.

The language server source is
`libs/cxpr/tools/language-server/server.js`. It delegates document analysis to
the compiled `cxpr_document_tooling` executable, so diagnostics and structural
information come from the C tooling rather than a JavaScript reimplementation
of the language. Its current LSP capabilities are incremental document sync,
diagnostics on open/change, document symbols, folding ranges, full semantic
tokens, same-document definition lookup, and concise symbol/function hover.
The extension contributes default colors for the semantic token categories
listed in `tools/vscode-cxpr/package.json`.

The tooling executable is selected in this order: the VS Code setting
`cxprLanguage.tooling.executable`, the `CXPR_DOCUMENT_TOOLING` environment
variable, then conventional workspace paths under `build/libs/cxpr` (including
`Debug` and `Release` subdirectories). If none exists, the server reports
`cxpr_document_tooling executable not found` as a document diagnostic.

Build the server before packaging the extension:

```sh
cd libs/cxpr/tools/language-server
npm install
npm run build

cd ../../../../tools/vscode-cxpr
npm install
npm run package:vsix
```

The server build uses esbuild and writes
`libs/cxpr/tools/language-server/dist/server.js`. The VSIX script requires the
`zip` CLI, bundles `extension.js`, copies that built server, constructs the
VSIX manifest, and writes
`tools/vscode-cxpr/dist/cxpr-language-0.1.0.vsix` for the current package
version. It fails with an actionable message when esbuild, zip, or the built
server is missing.

Install or replace the local extension with:

```sh
cd tools/vscode-cxpr
npm run install:extension
```

That script packages first, asks the selected VS Code CLI to uninstall
`local.cxpr-language`, and installs the new VSIX with `--force`. It uses `code`
by default; set `CXPR_VSCODE_CLI` to another compatible CLI path when needed.

TextMate patterns and semantic colors are presentation aids, not normative
grammar. For example, the grammar can color planned type words even though the
active parser does not accept typed declarations. The lexer/parser/validator
and their tests in the [source-of-truth table](#source-of-truth) define language
support.

## Deliberately unsupported or misleading forms

These forms are rejected or are not implemented by the current parser:

```cxpr
x = 1; y = 2                 # semicolon is not an expression separator
x within [1, 2]              # removed infix syntax
x in []                      # empty membership set
foo = 1                      # not an expression when parsed standalone
in period = 14               # defaulted input requires $period
meta legacy { kind = "x" }  # legacy metadata block
config { key: value }        # YAML mapping syntax in host blocks
```

Do not infer language support solely from a `.cxpr` fixture: some fixtures are
design contracts intentionally marked parse-only or invalid. Positive tests,
negative tests, and the active parser/validator determine the supported
language.
