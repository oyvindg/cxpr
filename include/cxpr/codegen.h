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

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Target description for a C-like backend (plain C, CUDA, WGSL, ...).
 *
 * `map_function` maps a cxpr builtin name and arity to the target's function
 * name (e.g. `min`/`max` are emitted as nested `fmin`/`fmax`; a CUDA target may
 * remap others). Return NULL to reject a function — transpilation then fails
 * with a clear error. When `map_function` (or the whole target) is NULL, a
 * default portable-C/CUDA mapping is used.
 */
typedef struct cxpr_c_target {
    const char* (*map_function)(const char* name, size_t argc, void* userdata);
    void* userdata;
} cxpr_c_target;

/**
 * @brief Transpile a single AST into a C expression string.
 *
 * Supported nodes: numbers, booleans, string literals, identifiers, `$params`
 * (emitted as the bare name), arithmetic/comparison/logical binary operators,
 * unary `-`/`!`, ternary `?:`, and function calls resolved through @p target.
 * `^`/`**` map to `pow()`, `%` to `fmod()`, `and`/`or`/`not` to `&&`/`||`/`!`.
 * Field/chain/producer/lookback nodes are rejected (host/series concepts with
 * no standalone C form).
 *
 * @param ast Expression AST to transpile.
 * @param target Optional target (NULL = default mapping).
 * @param err Optional error output, populated on failure.
 * @return Newly allocated C expression string (free with `free`), or NULL on
 *         an unsupported node, operator, or function.
 */
char* cxpr_ast_to_c(const cxpr_ast* ast, const cxpr_c_target* target, cxpr_error* err);

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

#ifdef __cplusplus
}
#endif

#endif /* CXPR_CODEGEN_H */
