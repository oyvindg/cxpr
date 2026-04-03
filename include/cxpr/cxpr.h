/**
 * @file cxpr.h
 * @brief C API for cxpr expression engine.
 *
 * Pure C11 interface for maximum portability and FFI compatibility.
 */

#ifndef CXPR_H
#define CXPR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Version
 * ═══════════════════════════════════════════════════════════════════════════ */

#define CXPR_VERSION_MAJOR 1
#define CXPR_VERSION_MINOR 0
#define CXPR_VERSION_PATCH 0

/* ═══════════════════════════════════════════════════════════════════════════
 * Opaque handles
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct cxpr_parser cxpr_parser;
typedef struct cxpr_ast cxpr_ast;
typedef struct cxpr_context cxpr_context;
typedef struct cxpr_registry cxpr_registry;
typedef struct cxpr_program cxpr_program;
typedef struct cxpr_formula_engine cxpr_formula_engine;
typedef struct cxpr_struct_value cxpr_struct_value;

/* ═══════════════════════════════════════════════════════════════════════════
 * Error handling
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Error codes returned by cxpr functions.
 */
typedef enum {
    CXPR_OK = 0,                    /**< No error */
    CXPR_ERR_SYNTAX,                /**< Syntax error in expression */
    CXPR_ERR_UNKNOWN_IDENTIFIER,    /**< Unknown variable or field */
    CXPR_ERR_UNKNOWN_FUNCTION,      /**< Unknown function name */
    CXPR_ERR_WRONG_ARITY,           /**< Wrong number of function arguments */
    CXPR_ERR_DIVISION_BY_ZERO,      /**< Division by zero */
    CXPR_ERR_CIRCULAR_DEPENDENCY,   /**< Circular dependency in formula engine */
    CXPR_ERR_TYPE_MISMATCH,         /**< Value type not accepted by operator/context */
    CXPR_ERR_OUT_OF_MEMORY          /**< Memory allocation failed */
} cxpr_error_code;

/**
 * @brief Runtime type tag for cxpr values.
 */
typedef enum {
    CXPR_FIELD_DOUBLE = 0,
    CXPR_FIELD_BOOL = 1,
    CXPR_FIELD_STRUCT = 2
} cxpr_field_type;

/**
 * @brief A typed cxpr value.
 */
typedef struct cxpr_field_value {
    cxpr_field_type type;
    union {
        double d;
        bool b;
        cxpr_struct_value* s;
    };
} cxpr_field_value;

/**
 * @brief Owned collection of named typed fields.
 */
struct cxpr_struct_value {
    const char** field_names;
    cxpr_field_value* field_values;
    size_t field_count;
};

/**
 * @brief Construct a double field value.
 * @param d Scalar double payload.
 * @return Typed field value tagged as `CXPR_FIELD_DOUBLE`.
 */
static inline cxpr_field_value cxpr_fv_double(double d) {
    return (cxpr_field_value){ .type = CXPR_FIELD_DOUBLE, .d = d };
}

/**
 * @brief Construct a bool field value.
 * @param b Boolean payload.
 * @return Typed field value tagged as `CXPR_FIELD_BOOL`.
 */
static inline cxpr_field_value cxpr_fv_bool(bool b) {
    return (cxpr_field_value){ .type = CXPR_FIELD_BOOL, .b = b };
}

/**
 * @brief Construct a struct field value.
 * @param s Nested struct payload.
 * @return Typed field value tagged as `CXPR_FIELD_STRUCT`.
 */
static inline cxpr_field_value cxpr_fv_struct(cxpr_struct_value* s) {
    return (cxpr_field_value){ .type = CXPR_FIELD_STRUCT, .s = s };
}

/**
 * @brief Allocate a deep-copied struct value.
 * @param field_names Field name array.
 * @param field_values Field value array parallel to `field_names`.
 * @param field_count Number of fields to copy.
 * @return Newly allocated struct value, or NULL on allocation failure.
 */
cxpr_struct_value* cxpr_struct_value_new(const char* const* field_names,
                                         const cxpr_field_value* field_values,
                                         size_t field_count);

/**
 * @brief Free a struct value and all nested owned data.
 * @param s Struct value to free. Safe to pass NULL.
 */
void cxpr_struct_value_free(cxpr_struct_value* s);

/**
 * @brief Error information structure.
 *
 * Populated by functions that can fail. The message field points to
 * a static string — do not free it.
 */
typedef struct cxpr_error {
    cxpr_error_code code;           /**< Error code */
    const char* message;          /**< Static string, do not free */
    size_t position;              /**< Byte position in source */
    size_t line;                  /**< Line number (1-based) */
    size_t column;                /**< Column number (1-based) */
} cxpr_error;

/**
 * @brief Get human-readable string for an error code.
 * @param code Error code
 * @return Static string description
 */
const char* cxpr_error_string(cxpr_error_code code);

/**
 * @brief Compute the internal string hash used by cxpr context lookups.
 *
 * This is primarily useful with the prehashed context write APIs in hot loops.
 *
 * @param str NUL-terminated key string
 * @return Hash value suitable for cxpr_context_set_prehashed() and
 *         cxpr_context_set_param_prehashed()
 */
unsigned long cxpr_hash_string(const char* str);

#include <cxpr/ast.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Context API (variable bindings)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new empty context.
 * @return Context handle, or NULL on allocation failure
 */
cxpr_context* cxpr_context_new(void);

/**
 * @brief Free a context.
 * @param ctx Context to free (NULL-safe)
 */
void cxpr_context_free(cxpr_context* ctx);

/**
 * @brief Clone a context (deep copy).
 * @param ctx Context to clone
 * @return New context with same bindings, or NULL on failure
 */
cxpr_context* cxpr_context_clone(const cxpr_context* ctx);

/**
 * @brief Create an overlay context that falls back to a parent context.
 * @param parent Parent context to query on misses.
 * @return New overlay context, or NULL on allocation failure.
 */
cxpr_context* cxpr_context_overlay_new(const cxpr_context* parent);

/**
 * @brief Set a runtime variable.
 * @param ctx Context
 * @param name Variable name
 * @param value Variable value
 */
void cxpr_context_set(cxpr_context* ctx, const char* name, double value);

/**
 * @brief Set a runtime variable using a caller-precomputed key hash.
 *
 * This is intended for hot loops that update the same named variables many
 * times. Precompute the hash once with cxpr_hash_string() and reuse it across
 * iterations to avoid re-hashing the key on every write.
 *
 * @param ctx Context
 * @param name Variable name
 * @param hash Hash previously returned by cxpr_hash_string(name)
 * @param value Variable value
 */
void cxpr_context_set_prehashed(cxpr_context* ctx, const char* name,
                                unsigned long hash, double value);

/**
 * @brief Get a runtime variable.
 * @param ctx Context
 * @param name Variable name
 * @param[out] found Set to true if found, false otherwise (can be NULL)
 * @return Variable value, or 0.0 if not found
 */
double cxpr_context_get(const cxpr_context* ctx, const char* name, bool* found);

/**
 * @brief Set a compile-time parameter ($variable).
 * @param ctx Context
 * @param name Parameter name (without $ prefix)
 * @param value Parameter value
 */
void cxpr_context_set_param(cxpr_context* ctx, const char* name, double value);

/**
 * @brief Set a compile-time parameter using a caller-precomputed key hash.
 *
 * This mirrors cxpr_context_set_prehashed() for workloads that repeatedly
 * update the same parameter names.
 *
 * @param ctx Context
 * @param name Parameter name (without $ prefix)
 * @param hash Hash previously returned by cxpr_hash_string(name)
 * @param value Parameter value
 */
void cxpr_context_set_param_prehashed(cxpr_context* ctx, const char* name,
                                      unsigned long hash, double value);

/* ═══════════════════════════════════════════════════════════════════════════
 * Slot API — pre-bound variable handles for hot-loop writes
 *
 * When the same set of variables is updated on every evaluation tick, the
 * normal cxpr_context_set() path recomputes the djb2 hash and probes the
 * hashmap on every call.  Binding a slot once and using cxpr_context_slot_set()
 * in the loop reduces each update to a single pointer dereference.
 *
 * Usage:
 *   // one-time setup, after all variables have been inserted:
 *   cxpr_context_slot slot_a;
 *   cxpr_context_slot_bind(ctx, "a", &slot_a);
 *
 *   // hot loop:
 *   while (ticking) {
 *       cxpr_context_slot_set(&slot_a, new_a);
 *       result = cxpr_ir_eval_double(program, ctx, reg, &err);
 *   }
 *
 * A slot becomes stale when new variables are added to the context after
 * binding (the underlying hashmap may grow and relocate).  Test with
 * cxpr_context_slot_valid() before entering a loop that runs for a long time,
 * and rebind if it returns false.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Pre-bound handle to one variable slot in a context.
 *
 * Treat both fields as opaque.  They are public only to allow stack allocation
 * without a heap round-trip.
 */
typedef struct {
    double* _ptr;  /**< direct pointer to value field; NULL when unbound */
    void*   _base; /**< ctx->variables.entries snapshot for validity check */
} cxpr_context_slot;

/**
 * @brief Bind a slot to a named variable.
 *
 * The variable must already exist in ctx (insert it with cxpr_context_set()
 * first).  Slots bound to an overlay context only resolve variables owned by
 * that context's own map, not by its parents.
 *
 * @param ctx  Target context.
 * @param name Variable name.
 * @param slot Output handle.  Set to an unbound state on failure.
 * @return true when the variable was found and the slot is ready to use.
 */
bool cxpr_context_slot_bind(cxpr_context* ctx, const char* name, cxpr_context_slot* slot);

/**
 * @brief Test whether a slot is still valid.
 *
 * A slot becomes invalid when new variables are added to ctx after binding
 * (the internal hashmap may grow and relocate its storage).  Call
 * cxpr_context_slot_bind() again to refresh.
 *
 * @param ctx  The context the slot was bound to.
 * @param slot Slot to test.
 * @return true when the slot is safe to use with cxpr_context_slot_set/get.
 */
bool cxpr_context_slot_valid(const cxpr_context* ctx, const cxpr_context_slot* slot);

/**
 * @brief Write a value via a pre-bound slot.
 *
 * The slot must be valid (verified with cxpr_context_slot_valid()).
 * No hash computation or hashmap probe is performed.
 *
 * @param slot  Bound slot (must be valid).
 * @param value New value to store.
 */
void cxpr_context_slot_set(cxpr_context_slot* slot, double value);

/**
 * @brief Read a value via a pre-bound slot.
 *
 * The slot must be valid (verified with cxpr_context_slot_valid()).
 *
 * @param slot Bound slot (must be valid).
 * @return The current variable value.
 */
double cxpr_context_slot_get(const cxpr_context_slot* slot);

/**
 * @brief Get a compile-time parameter ($variable).
 * @param ctx Context
 * @param name Parameter name (without $ prefix)
 * @param[out] found Set to true if found, false otherwise (can be NULL)
 * @return Parameter value, or 0.0 if not found
 */
double cxpr_context_get_param(const cxpr_context* ctx, const char* name, bool* found);

/**
 * @brief Clear all variables and parameters.
 * @param ctx Context
 */
void cxpr_context_clear(cxpr_context* ctx);

/**
 * @brief Clear cached producer structs while keeping explicit struct bindings.
 * @param ctx Context
 */
void cxpr_context_clear_cached_structs(cxpr_context* ctx);

/**
 * @brief Store a named native struct in the context.
 * @param ctx Destination context.
 * @param name Struct binding name.
 * @param value Struct value to deep-copy into the context.
 */
void cxpr_context_set_struct(cxpr_context* ctx, const char* name,
                             const cxpr_struct_value* value);

/**
 * @brief Look up a named native struct in the context chain.
 * @param ctx Context to query.
 * @param name Struct binding name.
 * @return Borrowed struct value owned by the context, or NULL when absent.
 */
const cxpr_struct_value* cxpr_context_get_struct(const cxpr_context* ctx,
                                                 const char* name);

/**
 * @brief Look up a typed field from a named native struct.
 * @param ctx Context to query.
 * @param name Struct binding name.
 * @param field Field name inside the struct.
 * @param[out] found Set to true on success, false on miss. May be NULL.
 * @return Borrowed typed field value. Struct pointers remain owned by the context.
 */
cxpr_field_value cxpr_context_get_field(const cxpr_context* ctx, const char* name,
                                        const char* field, bool* found);

/* ═══════════════════════════════════════════════════════════════════════════
 * Prefixed Field API
 *
 * Convenience functions for setting multiple fields with a common prefix.
 * Internally generates flat keys like "macd.histogram".
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Set multiple fields with a common prefix.
 *
 * Each field is stored as "prefix.field" in the context.
 *
 * @param ctx Context
 * @param prefix Common prefix (e.g., "macd")
 * @param fields Array of field names
 * @param values Array of field values (parallel to fields)
 * @param count Number of fields
 *
 * @code
 * const char* fields[] = {"histogram", "signalLine", "macdLine"};
 * double values[] = {0.5, 1.2, 1.5};
 * cxpr_context_set_fields(ctx, "macd", fields, values, 3);
 * // Sets: macd.histogram=0.5, macd.signalLine=1.2, macd.macdLine=1.5
 * @endcode
 */
void cxpr_context_set_fields(cxpr_context* ctx, const char* prefix,
                             const char* const* fields, const double* values,
                             size_t count);

/* ═══════════════════════════════════════════════════════════════════════════
 * Function Registry API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Function pointer type for synchronous registered functions.
 * @param args Array of evaluated argument values
 * @param argc Number of arguments
 * @param userdata User-provided context pointer
 * @return Function result as double
 */
typedef double (*cxpr_func_ptr)(const double* args, size_t argc, void* userdata);
typedef void (*cxpr_struct_producer_ptr)(const double* args, size_t argc,
                                         cxpr_field_value* out, size_t field_count,
                                         void* userdata);
/**
 * @brief Optional destructor for function user data.
 * @param userdata User data pointer to release (may be NULL)
 */
typedef void (*cxpr_userdata_free_fn)(void* userdata);

/**
 * @brief Create a new function registry with built-in math functions.
 * @return Registry handle, or NULL on allocation failure
 */
cxpr_registry* cxpr_registry_new(void);

/**
 * @brief Free a function registry.
 * @param reg Registry to free (NULL-safe)
 */
void cxpr_registry_free(cxpr_registry* reg);

/**
 * @brief Register a synchronous function.
 * @param reg Registry
 * @param name Function name (snake_case recommended)
 * @param func Function pointer
 * @param min_args Minimum argument count
 * @param max_args Maximum argument count (same as min for fixed arity)
 * @param userdata User data passed to func (can be NULL)
 * @param free_userdata Optional userdata cleanup callback (can be NULL)
 */
void cxpr_registry_add(cxpr_registry* reg, const char* name,
                       cxpr_func_ptr func, size_t min_args, size_t max_args,
                       void* userdata, cxpr_userdata_free_fn free_userdata);

/**
 * @brief Register a unary scalar function (`f(x)`).
 *
 * Convenience wrapper over `cxpr_registry_add(...)` that adapts
 * `double (*)(double)` to the cxpr registry ABI.
 *
 * @param reg Registry
 * @param name Function name (snake_case recommended)
 * @param func Unary scalar function pointer
 */
void cxpr_registry_add_unary(cxpr_registry* reg, const char* name,
                             double (*func)(double));

/**
 * @brief Register a binary scalar function (`f(a,b)`).
 *
 * Convenience wrapper over `cxpr_registry_add(...)` that adapts
 * `double (*)(double,double)` to the cxpr registry ABI.
 *
 * @param reg Registry
 * @param name Function name (snake_case recommended)
 * @param func Binary scalar function pointer
 */
void cxpr_registry_add_binary(cxpr_registry* reg, const char* name,
                              double (*func)(double, double));

/**
 * @brief Register a nullary scalar function (`f()`).
 *
 * Convenience wrapper over `cxpr_registry_add(...)` that adapts
 * `double (*)(void)` to the cxpr registry ABI.
 *
 * @param reg Registry
 * @param name Function name (snake_case recommended)
 * @param func Nullary scalar function pointer
 */
void cxpr_registry_add_nullary(cxpr_registry* reg, const char* name,
                               double (*func)(void));

/**
 * @brief Register a ternary scalar function (`f(a,b,c)`).
 *
 * Convenience wrapper over `cxpr_registry_add(...)` that adapts
 * `double (*)(double,double,double)` to the cxpr registry ABI.
 *
 * @param reg Registry
 * @param name Function name (snake_case recommended)
 * @param func Ternary scalar function pointer
 */
void cxpr_registry_add_ternary(cxpr_registry* reg, const char* name,
                               double (*func)(double, double, double));

/**
 * @brief Look up a function in the registry.
 *
 * Used for validation and host-side introspection.
 *
 * @param reg Registry
 * @param name Function name
 * @param[out] min_args Minimum arity (can be NULL)
 * @param[out] max_args Maximum arity (can be NULL)
 * @return true if function exists, false otherwise
 */
bool cxpr_registry_lookup(const cxpr_registry* reg, const char* name,
                        size_t* min_args, size_t* max_args);

/**
 * @brief Call a registered synchronous function directly.
 *
 * Useful for host-side direct dispatch when an AST is not required.
 *
 * @param reg Registry
 * @param name Function name
 * @param args Array of argument values
 * @param argc Number of arguments
 * @param[out] err Error output (can be NULL)
 * @return Function result, or NaN on error
 */
double cxpr_registry_call(const cxpr_registry* reg, const char* name,
                          const double* args, size_t argc, cxpr_error* err);

/**
 * @brief Register a struct-aware function.
 *
 * The function is called with the expanded scalar arguments produced by
 * looking up each field for every identifier argument in the context.
 * For example, registering with fields={"x","y","z"}, fields_per_arg=3,
 * struct_argc=2 lets the expression `distance3(goal, pose)` be written
 * instead of `distance3(goal.x, goal.y, goal.z, pose.x, pose.y, pose.z)`.
 * The underlying @p func still receives a flat `double*` array.
 *
 * @param reg             Registry
 * @param name            Function name
 * @param func            Function pointer (receives struct_argc*fields_per_arg doubles)
 * @param fields          Field names to expand (e.g. {"x","y","z"})
 * @param fields_per_arg  Number of fields per struct argument
 * @param struct_argc     Number of struct arguments the expression accepts
 * @param userdata        User data passed to func (can be NULL)
 * @param free_userdata   Optional userdata cleanup callback (can be NULL)
 */
void cxpr_registry_add_fn(cxpr_registry* reg, const char* name,
                                  cxpr_func_ptr func,
                                  const char* const* fields, size_t fields_per_arg,
                                  size_t struct_argc,
                                  void* userdata, cxpr_userdata_free_fn free_userdata);

/**
 * @brief Register a struct-producing function.
 *
 * Registered producers can be used as `name.field`, `name(args).field`, and
 * as direct typed calls through `cxpr_ast_eval(...)` / `cxpr_ir_eval(...)`.
 * The typed call path yields a `CXPR_FIELD_STRUCT` value backed by the
 * evaluation context cache.
 *
 * @param reg Registry to add to.
 * @param name Producer name used from expressions.
 * @param func Producer callback that fills `field_count` typed outputs.
 * @param min_args Minimum supported argument count.
 * @param max_args Maximum supported argument count.
 * @param fields Output field names in the same order as `out`.
 * @param field_count Number of produced fields.
 * @param userdata User data passed to the producer.
 * @param free_userdata Optional destructor for `userdata`.
 */
void cxpr_registry_add_struct(cxpr_registry* reg, const char* name,
                                       cxpr_struct_producer_ptr func,
                                       size_t min_args, size_t max_args,
                                       const char* const* fields, size_t field_count,
                                       void* userdata,
                                       cxpr_userdata_free_fn free_userdata);

/**
 * @brief Register all built-in math functions to a registry.
 *
 * @param reg Registry to populate
 */
void cxpr_register_builtins(cxpr_registry* reg);

/**
 * @brief Register an expression-based function from a definition string.
 *
 * Parses a definition of the form `name(p1, p2, ...) => body` and registers
 * a callable function.  Parameters that appear as `param.field` in the body
 * are treated as struct arguments (caller passes an identifier, fields are
 * read from the context).  Parameters with no dot-access are plain scalars.
 *
 * @code
 * // Struct params — fields inferred from body
 * cxpr_registry_define_fn(reg,
 *     "distance3(goal, pose) => "
 *     "sqrt((goal.x-pose.x)^2 + (goal.y-pose.y)^2 + (goal.z-pose.z)^2)");
 *
 * // Scalar params
 * cxpr_registry_define_fn(reg, "sum(a, b) => a + b");
 *
 * // Mixed struct + $params
 * cxpr_registry_define_fn(reg, "clamp_val(p) => clamp(p.value, $lo, $hi)");
 * @endcode
 *
 * @param reg  Registry to add to
 * @param def  Null-terminated definition string
 * @return     cxpr_error — code is CXPR_OK on success
 */
cxpr_error cxpr_registry_define_fn(cxpr_registry* reg, const char* def);

/* ═══════════════════════════════════════════════════════════════════════════
 * Formula Engine API (multi-formula with dependencies)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new formula engine.
 * @param reg Function registry to use for evaluation
 * @return Formula engine handle, or NULL on allocation failure
 */
cxpr_formula_engine* cxpr_formula_engine_new(const cxpr_registry* reg);

/**
 * @brief Free a formula engine.
 * @param engine Engine to free (NULL-safe)
 */
void cxpr_formula_engine_free(cxpr_formula_engine* engine);

/**
 * @brief A formula definition used for batch registration.
 */
typedef struct {
    const char* name;
    const char* expression;
} cxpr_formula_def;

/**
 * @brief Add a named formula to the engine.
 * @param engine Formula engine
 * @param name Formula name (used as variable name for dependencies)
 * @param expression Expression string
 * @param[out] err Error output (can be NULL)
 * @return true on success, false on parse error
 */
bool cxpr_formula_add(cxpr_formula_engine* engine, const char* name,
                    const char* expression, cxpr_error* err);

/**
 * @brief Add multiple named formulas to the engine.
 *
 * Stops at the first failure and rolls back formulas added by this call.
 *
 * @param engine Formula engine
 * @param defs Array of formula definitions
 * @param count Number of definitions in `defs`
 * @param[out] err Error output (can be NULL)
 * @return true on success, false on invalid input or parse error
 */
bool cxpr_formulas_add(cxpr_formula_engine* engine, const cxpr_formula_def* defs,
                       size_t count, cxpr_error* err);

/**
 * @brief Compile the formula engine (resolve dependencies).
 *
 * Performs topological sort (Kahn's algorithm) and cycle detection.
 *
 * @param engine Formula engine
 * @param[out] err Error output (can be NULL)
 * @return true on success, false on circular dependency
 */
bool cxpr_formula_compile(cxpr_formula_engine* engine, cxpr_error* err);

/**
 * @brief Evaluate all formulas in dependency order.
 * @param engine Compiled formula engine
 * @param ctx Context with external variables (results are also set here)
 * @param[out] err Error output (can be NULL)
 */
void cxpr_formula_eval_all(cxpr_formula_engine* engine, cxpr_context* ctx, cxpr_error* err);

/**
 * @brief Get the result of a named formula after evaluation.
 * @param engine Formula engine
 * @param name Formula name
 * @param[out] found Set to true if found (can be NULL)
 * @return Formula result, or 0.0 if not found
 */
double cxpr_formula_get(const cxpr_formula_engine* engine, const char* name, bool* found);

/**
 * @brief Get evaluation order after compilation.
 *
 * codegen uses this to generate code that evaluates formulas
 * in the correct dependency order.
 *
 * @param engine Compiled formula engine
 * @param[out] names Output array for formula names (caller provides)
 * @param max_names Maximum names to return
 * @return Number of formulas in evaluation order
 */
size_t cxpr_formula_eval_order(const cxpr_formula_engine* engine,
                              const char** names, size_t max_names);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_H */
