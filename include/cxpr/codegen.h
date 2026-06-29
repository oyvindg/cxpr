/**
 * @file codegen.h
 * @brief Transpile cxpr ASTs into C (and C-like, e.g. CUDA/WGSL) source.
 *
 * `cxpr_ast_to_c` renders one expression AST as a C expression string;
 * `cxpr_exprset_to_c` renders a set of interdependent named expressions as a
 * block of C declarations, ordered so every reference follows its definition.
 *
 * This is the codegen counterpart to the runtime evaluator: instead of
 * evaluating an expression, emit equivalent native source so it can be compiled
 * (for a hot loop, a GPU kernel via a runtime compiler, etc.). The output is
 * domain-agnostic — target-specific function names are supplied by the caller
 * through `cxpr_c_target`.
 */

#ifndef CXPR_CODEGEN_H
#define CXPR_CODEGEN_H

#include <cxpr/ast.h>
#include <cxpr/types.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXPR_C_TARGET_API_VERSION 1u

typedef char* (*cxpr_c_emit_leaf_at_offset_fn)(const cxpr_ast* ast,
                                               unsigned lookback_offset,
                                               void* userdata,
                                               cxpr_error* err);

typedef bool (*cxpr_c_emit_offset_fn)(void* userdata,
                                      const cxpr_ast* ast,
                                      int lookback_offset,
                                      cxpr_error* err);

/**
 * @brief Optionally emit a whole FUNCTION_CALL node as target-specific source.
 *
 * Consulted before cxpr's own call emission (builtin name mapping, the
 * `rising`/`falling`/`repeat`/`min`/`max` expansions). Lets a target render a
 * call its own way — e.g. a memoized value referenced as a precomputed
 * variable, or a source accessor lowered to an array index — instead of as a
 * `name(args...)` call. Set `*handled` to true and return a newly allocated
 * string (free with `free`) to take ownership of the node; on error inside a
 * handled node, set `*handled` true, populate @p err, and return NULL. Set
 * `*handled` false (return value ignored) to fall back to cxpr's default
 * emission. The target may recurse into sub-arguments via
 * `cxpr_ast_to_c_at_offset` to keep nested operator/lookback handling in cxpr.
 */
typedef char* (*cxpr_c_emit_call_at_offset_fn)(const cxpr_ast* ast,
                                               unsigned lookback_offset,
                                               void* userdata,
                                               bool* handled,
                                               cxpr_error* err);

/**
 * @brief Target description for a C-like backend (plain C, CUDA, WGSL, ...).
 *
 * `map_function` maps a cxpr builtin name and arity to the target's function
 * name (e.g. `min`/`max` are emitted as nested `fmin`/`fmax`; a CUDA target may
 * remap others). Return NULL to reject a function — transpilation then fails
 * with a clear error. When `map_function` (or the whole target) is NULL, a
 * default portable-C/CUDA mapping is used.
 *
 * `emit_leaf_at_offset` (optional) lets the target render identifier/variable/
 * field leaves itself, threading the current lookback offset; `emit_call_at_offset`
 * (optional) does the same for whole function-call nodes. Both are gated on
 * `api_version == CXPR_C_TARGET_API_VERSION`.
 */
typedef struct cxpr_c_target {
    const char* (*map_function)(const char* name, size_t argc, void* userdata);
    void* userdata;
    unsigned api_version;
    cxpr_c_emit_leaf_at_offset_fn emit_leaf_at_offset;
    cxpr_c_emit_call_at_offset_fn emit_call_at_offset;
} cxpr_c_target;

/**
 * @brief Transpile a single AST into a C expression string.
 *
 * Supported nodes: numbers, booleans, string literals, identifiers, `$params`
 * (emitted as the bare name), arithmetic/comparison/logical binary operators,
 * unary `-`/`!`, ternary `?:`, and function calls resolved through @p target.
 * `^`/`**` map to `pow()`, `%` to `fmod()`, `and`/`or`/`not` to `&&`/`||`/`!`.
 * Lookback nodes are supported only when @p target supplies
 * `emit_leaf_at_offset`; field/chain/producer nodes are rejected unless the
 * target can emit them as offset-aware leaves.
 *
 * @param ast Expression AST to transpile.
 * @param target Optional target (NULL = default mapping).
 * @param err Optional error output, populated on failure.
 * @return Newly allocated C expression string (free with `free`), or NULL on
 *         an unsupported node, operator, or function.
 */
char* cxpr_ast_to_c(const cxpr_ast* ast, const cxpr_c_target* target, cxpr_error* err);

/**
 * @brief Transpile a single AST into a C expression string at a lookback offset.
 *
 * As `cxpr_ast_to_c`, but every leaf/lookback is resolved relative to
 * @p lookback_offset (added to any `expr[n]` offsets encountered). Intended for
 * targets whose `emit_call_at_offset` recurses into sub-arguments: pass the
 * offset handed to the hook so nested lookback stays correct.
 *
 * @param ast Expression AST to transpile.
 * @param lookback_offset Base lookback offset applied to leaves.
 * @param target Optional target (NULL = default mapping).
 * @param err Optional error output, populated on failure.
 * @return Newly allocated C expression string (free with `free`), or NULL on error.
 */
char* cxpr_ast_to_c_at_offset(const cxpr_ast* ast, unsigned lookback_offset,
                              const cxpr_c_target* target, cxpr_error* err);

/**
 * @brief Apply cxpr's native lookback offset rule to one LOOKBACK node.
 *
 * This helper centralizes the `expr[n]` codegen rule used by backends with
 * custom emitters: validate that `n` is a non-negative integer literal, add it
 * to @p current_offset, then delegate emission of the target expression to the
 * host callback. The host owns concrete leaf layout.
 */
bool cxpr_codegen_emit_lookback_offset(const cxpr_ast* ast,
                                       int current_offset,
                                       cxpr_c_emit_offset_fn emit,
                                       void* userdata,
                                       cxpr_error* err);

/** @brief One named expression for set transpilation. */
typedef struct cxpr_c_named_expr {
    const char* name;       /**< Identifier the expression is bound to. */
    const cxpr_ast* ast;    /**< Parsed expression. */
} cxpr_c_named_expr;

/**
 * @brief Transpile a set of interdependent named expressions into a C block.
 *
 * Emits one `"<decl_type> <name> = <expr>;\n"` per expression, ordered by a
 * topological sort over references between the names, so each definition
 * precedes its uses. Names referenced but not defined in the set are treated as
 * external inputs and left as-is.
 *
 * @param exprs Named expressions.
 * @param count Number of entries.
 * @param decl_type C type for each declaration (e.g. `"double"`, `"const double"`).
 * @param target Optional target (NULL = default mapping).
 * @param err Optional error output, populated on failure.
 * @return Newly allocated C block (free with `free`), or NULL on a dependency
 *         cycle, an unsupported node, or an unknown function.
 */
char* cxpr_exprset_to_c(const cxpr_c_named_expr* exprs, size_t count,
                        const char* decl_type, const cxpr_c_target* target,
                        cxpr_error* err);

/**
 * @brief Transpile an interdependent expression set into a complete C function.
 *
 * Emits a result `struct` (one `scalar_type` field per expression name) and a
 * function that takes one `scalar_type` parameter per input, computes the
 * expressions as locals in dependency order, packs them into the struct, and
 * returns it. Builds on `cxpr_exprset_to_c`. The caller still owns any file
 * scaffolding (include guard, target macros like `__host__ __device__`).
 *
 * Example output:
 * ```c
 * typedef struct State { double r_s; double f; } State;
 * <qualifiers> State eval(double r, double G) { ...locals...; State _cx_out; ...; return _cx_out; }
 * ```
 *
 * @param qualifiers Leading qualifiers for the function (e.g. "static inline"), or NULL.
 * @param return_struct Name of the emitted result struct (fields = expression names).
 * @param scalar_type Scalar C type for fields, params, and locals (e.g. "double").
 * @param function_name Generated function name.
 * @param inputs Parameter names (each typed `scalar_type`).
 * @param input_count Number of inputs (0 emits a `void` parameter list).
 * @param exprs Named expressions (their names become struct fields).
 * @param count Number of expressions.
 * @param target Optional function-name mapping (NULL = default).
 * @param err Optional error output.
 * @return Newly allocated C source (free with `free`), or NULL on error.
 */
char* cxpr_exprset_to_c_function(const char* qualifiers, const char* return_struct,
                                 const char* scalar_type, const char* function_name,
                                 const char* const* inputs, size_t input_count,
                                 const cxpr_c_named_expr* exprs, size_t count,
                                 const cxpr_c_target* target, cxpr_error* err);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_CODEGEN_H */
