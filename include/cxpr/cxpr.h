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
typedef struct {
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
typedef struct {
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

/* ═══════════════════════════════════════════════════════════════════════════
 * Parser API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Create a new parser instance.
 * @return Parser handle, or NULL on allocation failure
 */
cxpr_parser* cxpr_parser_new(void);

/**
 * @brief Free a parser instance.
 * @param p Parser to free (NULL-safe)
 */
void cxpr_parser_free(cxpr_parser* p);

/**
 * @brief Parse expression string into AST.
 * @param p Parser instance
 * @param expression Null-terminated expression string
 * @param[out] err Error output (can be NULL)
 * @return AST on success, NULL on error
 */
cxpr_ast* cxpr_parse(cxpr_parser* p, const char* expression, cxpr_error* err);

/**
 * @brief Free an AST.
 * @param ast AST to free (NULL-safe)
 */
void cxpr_ast_free(cxpr_ast* ast);

/**
 * @brief Construct a NUMBER AST node.
 * @param value Numeric literal payload.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_number(double value);

/**
 * @brief Construct a BOOL AST node.
 * @param value Boolean literal payload.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_bool(bool value);

/**
 * @brief Construct an IDENTIFIER AST node.
 */
cxpr_ast* cxpr_ast_new_identifier(const char* name);

/**
 * @brief Construct a VARIABLE AST node (without '$' prefix).
 */
cxpr_ast* cxpr_ast_new_variable(const char* name);

/**
 * @brief Construct a FIELD_ACCESS AST node.
 */
cxpr_ast* cxpr_ast_new_field_access(const char* object, const char* field);

/**
 * @brief Construct a PRODUCER_ACCESS AST node.
 * @param name Producer name.
 * @param args Owned argument AST array.
 * @param argc Number of arguments.
 * @param field Output field name.
 * @return Newly allocated AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_producer_access(const char* name, cxpr_ast** args,
                                       size_t argc, const char* field);

/**
 * @brief Construct a BINARY_OP AST node.
 */
cxpr_ast* cxpr_ast_new_binary_op(int op, cxpr_ast* left, cxpr_ast* right);

/**
 * @brief Construct a UNARY_OP AST node.
 */
cxpr_ast* cxpr_ast_new_unary_op(int op, cxpr_ast* operand);

/**
 * @brief Construct a FUNCTION_CALL AST node.
 */
cxpr_ast* cxpr_ast_new_function_call(const char* name, cxpr_ast** args, size_t argc);

/**
 * @brief Construct a TERNARY AST node.
 */
cxpr_ast* cxpr_ast_new_ternary(cxpr_ast* condition, cxpr_ast* true_branch, cxpr_ast* false_branch);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Inspection API (for codegen tree-walking)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief AST node types.
 */
typedef enum {
    CXPR_NODE_NUMBER,               /**< Numeric literal (double) */
    CXPR_NODE_BOOL,                 /**< Boolean literal */
    CXPR_NODE_IDENTIFIER,           /**< Variable reference (e.g. "rsi") */
    CXPR_NODE_VARIABLE,             /**< Parameter reference (e.g. "$oversold") */
    CXPR_NODE_FIELD_ACCESS,         /**< Dotted field (e.g. "macd.histogram") */
    CXPR_NODE_CHAIN_ACCESS,         /**< Chained field access (e.g. "a.b.c") */
    CXPR_NODE_PRODUCER_ACCESS,      /**< Struct producer field access (e.g. "macd(12,3).line") */
    CXPR_NODE_BINARY_OP,            /**< Binary operator (+, -, *, /, <, and, etc.) */
    CXPR_NODE_UNARY_OP,             /**< Unary operator (-, not) */
    CXPR_NODE_FUNCTION_CALL,        /**< Function call (e.g. "cross_above(a, b)") */
    CXPR_NODE_TERNARY               /**< Ternary conditional (cond ? a : b) */
} cxpr_node_type;

/**
 * @brief Get the type of an AST node.
 * @param ast AST node
 * @return Node type
 */
cxpr_node_type cxpr_ast_type(const cxpr_ast* ast);

/**
 * @brief Get the numeric value of a NUMBER node.
 * @param ast AST node (must be CXPR_NODE_NUMBER)
 * @return The double value
 */
double cxpr_ast_number_value(const cxpr_ast* ast);

/**
 * @brief Get the name of an IDENTIFIER node.
 * @param ast AST node (must be CXPR_NODE_IDENTIFIER)
 * @return Identifier name (owned by AST, do not free)
 */
const char* cxpr_ast_identifier_name(const cxpr_ast* ast);

/**
 * @brief Get the name of a VARIABLE node (without the $ prefix).
 * @param ast AST node (must be CXPR_NODE_VARIABLE)
 * @return Variable name (owned by AST, do not free)
 */
const char* cxpr_ast_variable_name(const cxpr_ast* ast);

/**
 * @brief Get the object name of a FIELD_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_FIELD_ACCESS)
 * @return Object name, e.g. "macd" from "macd.histogram"
 */
const char* cxpr_ast_field_object(const cxpr_ast* ast);

/**
 * @brief Get the field name of a FIELD_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_FIELD_ACCESS)
 * @return Field name, e.g. "histogram" from "macd.histogram"
 */
const char* cxpr_ast_field_name(const cxpr_ast* ast);
size_t cxpr_ast_chain_depth(const cxpr_ast* ast);
const char* cxpr_ast_chain_segment(const cxpr_ast* ast, size_t index);

/**
 * @brief Get the operator of a BINARY_OP or UNARY_OP node.
 * @param ast AST node (must be CXPR_NODE_BINARY_OP or CXPR_NODE_UNARY_OP)
 * @return Operator as cxpr_token_type (see internal.h for values)
 */
int cxpr_ast_operator(const cxpr_ast* ast);

/**
 * @brief Get the left child of a BINARY_OP node.
 * @param ast AST node (must be CXPR_NODE_BINARY_OP)
 * @return Left child (owned by parent, do not free)
 */
const cxpr_ast* cxpr_ast_left(const cxpr_ast* ast);

/**
 * @brief Get the right child of a BINARY_OP node.
 * @param ast AST node (must be CXPR_NODE_BINARY_OP)
 * @return Right child (owned by parent, do not free)
 */
const cxpr_ast* cxpr_ast_right(const cxpr_ast* ast);

/**
 * @brief Get the operand of a UNARY_OP node.
 * @param ast AST node (must be CXPR_NODE_UNARY_OP)
 * @return Operand (owned by parent, do not free)
 */
const cxpr_ast* cxpr_ast_operand(const cxpr_ast* ast);

/**
 * @brief Get the function name of a FUNCTION_CALL node.
 * @param ast AST node (must be CXPR_NODE_FUNCTION_CALL)
 * @return Function name (owned by AST, do not free)
 */
const char* cxpr_ast_function_name(const cxpr_ast* ast);

/**
 * @brief Get the argument count of a FUNCTION_CALL node.
 * @param ast AST node (must be CXPR_NODE_FUNCTION_CALL)
 * @return Number of arguments
 */
size_t cxpr_ast_function_argc(const cxpr_ast* ast);

/**
 * @brief Get an argument of a FUNCTION_CALL node by index.
 * @param ast AST node (must be CXPR_NODE_FUNCTION_CALL)
 * @param index Argument index (0-based)
 * @return Argument AST (owned by parent, do not free), or NULL if out of bounds
 */
const cxpr_ast* cxpr_ast_function_arg(const cxpr_ast* ast, size_t index);

/**
 * @brief Get the condition of a TERNARY node.
 * @param ast AST node (must be CXPR_NODE_TERNARY)
 * @return Condition AST (owned by parent, do not free)
 */
const cxpr_ast* cxpr_ast_ternary_condition(const cxpr_ast* ast);

/**
 * @brief Get the true-branch of a TERNARY node.
 * @param ast AST node (must be CXPR_NODE_TERNARY)
 * @return True branch AST (owned by parent, do not free)
 */
const cxpr_ast* cxpr_ast_ternary_true_branch(const cxpr_ast* ast);

/**
 * @brief Get the false-branch of a TERNARY node.
 * @param ast AST node (must be CXPR_NODE_TERNARY)
 * @return False branch AST (owned by parent, do not free)
 */
const cxpr_ast* cxpr_ast_ternary_false_branch(const cxpr_ast* ast);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Reference Extraction API (critical for codegen)
 *
 * These functions walk the AST and extract unique references.
 * codegen uses them to:
 *   - cxpr_ast_references(): find which indicators/variables an expression needs
 *   - cxpr_ast_functions():  find which functions are called (for validation)
 *   - cxpr_ast_variables():  find which $params are used (for substitution)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Extract all unique identifier and field references from an AST.
 *
 * Walks the AST and collects all CXPR_NODE_IDENTIFIER names and
 * CXPR_NODE_FIELD_ACCESS full keys (e.g. "macd.histogram").
 * Does NOT include function names or $variables.
 *
 * @param ast AST to analyze
 * @param[out] names Output array for unique reference names
 * @param max_names Maximum names to return
 * @return Number of unique references found (may exceed max_names)
 */
size_t cxpr_ast_references(const cxpr_ast* ast, const char** names, size_t max_names);

/**
 * @brief Extract all unique function names called in an AST.
 *
 * Walks the AST and collects all CXPR_NODE_FUNCTION_CALL names.
 *
 * @param ast AST to analyze
 * @param[out] names Output array for unique function names
 * @param max_names Maximum names to return
 * @return Number of unique function names found (may exceed max_names)
 */
size_t cxpr_ast_functions_used(const cxpr_ast* ast, const char** names, size_t max_names);

/**
 * @brief Extract all unique $variable names from an AST.
 *
 * Walks the AST and collects all CXPR_NODE_VARIABLE names (without $ prefix).
 *
 * @param ast AST to analyze
 * @param[out] names Output array for unique variable names
 * @param max_names Maximum names to return
 * @return Number of unique variable names found (may exceed max_names)
 */
size_t cxpr_ast_variables_used(const cxpr_ast* ast, const char** names, size_t max_names);

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
 * Evaluator API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Evaluate an AST to a typed value.
 * @param ast Parsed AST
 * @param ctx Variable context
 * @param reg Function registry
 * @param[out] err Error output (can be NULL)
 * @return Evaluation result, or a sentinel double NaN on error
 */
cxpr_field_value cxpr_ast_eval(const cxpr_ast* ast, const cxpr_context* ctx,
                               const cxpr_registry* reg, cxpr_error* err);

/**
 * @brief Evaluate an AST and require a double result.
 * @param ast Parsed AST.
 * @param ctx Variable context.
 * @param reg Function registry.
 * @param[out] err Error output. Receives `CXPR_ERR_TYPE_MISMATCH` on non-double results.
 * @return Double result, or NaN on error.
 */
double cxpr_ast_eval_double(const cxpr_ast* ast, const cxpr_context* ctx,
                            const cxpr_registry* reg, cxpr_error* err);

/**
 * @brief Evaluate an AST to a boolean value.
 *
 * Requires the expression result to be a bool.
 *
 * @param ast Parsed AST
 * @param ctx Variable context
 * @param reg Function registry
 * @param[out] err Error output (can be NULL)
 * @return bool result, or false on error
 */
bool cxpr_ast_eval_bool(const cxpr_ast* ast, const cxpr_context* ctx,
                        const cxpr_registry* reg, cxpr_error* err);

/* ═══════════════════════════════════════════════════════════════════════════
 * Compiled Program API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Compile an AST into a reusable internal execution plan.
 *
 * The current implementation may mix native IR instructions with
 * fallback steps that preserve existing evaluator semantics.
 *
 * @param ast Parsed AST
 * @param reg Function registry used for validation/runtime compatibility
 * @param[out] err Error output (can be NULL)
 * @return Compiled program, or NULL on error
 */
cxpr_program* cxpr_compile(const cxpr_ast* ast, const cxpr_registry* reg, cxpr_error* err);

/**
 * @brief Evaluate a compiled program to a typed value.
 *
 * @param prog Compiled program
 * @param ctx Variable context
 * @param reg Function registry
 * @param[out] err Error output (can be NULL)
 * @return Evaluation result, or a sentinel double NaN on error
 */
cxpr_field_value cxpr_ir_eval(const cxpr_program* prog, const cxpr_context* ctx,
                              const cxpr_registry* reg, cxpr_error* err);

/**
 * @brief Evaluate a compiled program and require a double result.
 * @param prog Compiled program.
 * @param ctx Variable context.
 * @param reg Function registry.
 * @param[out] err Error output. Receives `CXPR_ERR_TYPE_MISMATCH` on non-double results.
 * @return Double result, or NaN on error.
 */
double cxpr_ir_eval_double(const cxpr_program* prog, const cxpr_context* ctx,
                           const cxpr_registry* reg, cxpr_error* err);

/**
 * @brief Evaluate a compiled program as boolean.
 *
 * @param prog Compiled program
 * @param ctx Variable context
 * @param reg Function registry
 * @param[out] err Error output (can be NULL)
 * @return true if result != 0.0, false otherwise
 */
bool cxpr_ir_eval_bool(const cxpr_program* prog, const cxpr_context* ctx,
                       const cxpr_registry* reg, cxpr_error* err);

/**
 * @brief Free a compiled program.
 * @param prog Compiled program (NULL-safe)
 */
void cxpr_program_free(cxpr_program* prog);

/**
 * @brief Dump a compiled program's instruction stream to a text stream.
 *
 * Useful for debugging and benchmarking compiled IR.
 *
 * @param prog Compiled program
 * @param out Output stream (uses stdout when NULL)
 */
void cxpr_program_dump(const cxpr_program* prog, FILE* out);

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
