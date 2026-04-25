/**
 * @file internal.h
 * @brief Internal IR data structures and helpers shared across cxpr modules.
 */
#ifndef CXPR_IR_INTERNAL_H
#define CXPR_IR_INTERNAL_H

#include "../limits.h"
#include "../context/state.h"
#include "../registry/internal.h"
/** @brief Result kind is not known statically for this IR program. */
#define CXPR_IR_RESULT_UNKNOWN 0
/** @brief IR program is known to produce a numeric scalar result. */
#define CXPR_IR_RESULT_DOUBLE 1
/** @brief IR program is known to produce a boolean scalar result. */
#define CXPR_IR_RESULT_BOOL 2

/**
 * @brief Minimal opcode set for IR v1.
 *
 * V1 intentionally supports only constants, runtime variables, $params,
 * basic arithmetic, and return.
 */
typedef enum {
    CXPR_OP_PUSH_CONST,
    CXPR_OP_PUSH_BOOL,
    CXPR_OP_LOAD_LOCAL,
    CXPR_OP_LOAD_LOCAL_SQUARE,
    CXPR_OP_LOAD_VAR,
    CXPR_OP_LOAD_VAR_SQUARE,
    CXPR_OP_LOAD_PARAM,
    CXPR_OP_LOAD_PARAM_SQUARE,
    CXPR_OP_LOAD_FIELD,
    CXPR_OP_LOAD_FIELD_SQUARE,
    CXPR_OP_LOAD_CHAIN,
    CXPR_OP_ADD,
    CXPR_OP_SUB,
    CXPR_OP_MUL,
    CXPR_OP_SQUARE,
    CXPR_OP_DIV,
    CXPR_OP_MOD,
    CXPR_OP_CMP_EQ,
    CXPR_OP_CMP_NEQ,
    CXPR_OP_CMP_LT,
    CXPR_OP_CMP_LTE,
    CXPR_OP_CMP_GT,
    CXPR_OP_CMP_GTE,
    CXPR_OP_NOT,
    CXPR_OP_NEG,
    CXPR_OP_SIGN,
    CXPR_OP_SQRT,
    CXPR_OP_ABS,
    CXPR_OP_FLOOR,
    CXPR_OP_CEIL,
    CXPR_OP_ROUND,
    CXPR_OP_POW,
    CXPR_OP_CLAMP,
    CXPR_OP_CALL_PRODUCER,
    CXPR_OP_CALL_PRODUCER_CONST,
    CXPR_OP_CALL_PRODUCER_CONST_FIELD,
    CXPR_OP_GET_FIELD,
    CXPR_OP_CALL_UNARY,
    CXPR_OP_CALL_BINARY,
    CXPR_OP_CALL_TERNARY,
    CXPR_OP_CALL_FUNC,
    CXPR_OP_CALL_DEFINED,
    CXPR_OP_CALL_AST,
    CXPR_OP_JUMP,
    CXPR_OP_JUMP_IF_FALSE,
    CXPR_OP_JUMP_IF_TRUE,
    CXPR_OP_RETURN
} cxpr_opcode;

/**
 * @brief A single IR instruction.
 *
 * The operand is interpreted according to opcode:
 * - PUSH_CONST: literal double in value
 * - LOAD_VAR / LOAD_PARAM / GET_FIELD: borrowed symbol name in name
 * - CALL_PRODUCER_CONST_FIELD: cache key in name, field name in aux_name
 * - CALL_PRODUCER_CONST_FIELD: payload points to owned constant double args
 * - CALL_AST: borrowed AST pointer in ast
 * - JUMP / conditional jumps: target index in index
 * - arithmetic / comparisons / return: no extra operand
 */
typedef struct {
    cxpr_opcode op;
    /* 4 bytes implicit padding */
    const char* name;
    const char* aux_name;
    const void* payload;
    const cxpr_func_entry* func;
    union {
        double value;        /* PUSH_CONST, PUSH_BOOL */
        unsigned long hash;  /* LOAD_VAR, LOAD_PARAM, LOAD_FIELD, LOAD_CHAIN */
        size_t index;        /* LOAD_LOCAL, CALL_*, CALL_DEFINED, CALL_PRODUCER, JUMP* */
        const cxpr_ast* ast; /* CALL_AST */
    };
} cxpr_ir_instr;

_Static_assert(sizeof(cxpr_ir_instr) <= 48, "cxpr_ir_instr for stor");

/**
 * @brief Cached LOAD_VAR / LOAD_PARAM resolution for one IR instruction.
 */
typedef struct {
    const cxpr_context* request_ctx;        /**< Context chain root used for the cached lookup */
    const cxpr_context* owner_ctx;          /**< Context owning the cached entry */
    cxpr_hashmap_entry* entries_base;       /**< Owner map base when the entry was cached */
    size_t              slot;               /**< Index into entries_base for the cached entry */
    unsigned long       shadow_version;     /**< Structural version summary below the owner */
} cxpr_ir_lookup_cache;

/**
 * @brief Internal compiled program representation.
 *
 * This stays internal until a public cxpr_program API is introduced.
 */
typedef struct {
    cxpr_ir_instr* code;        /**< Owned instruction array */
    size_t count;               /**< Number of instructions */
    size_t capacity;            /**< Allocated instruction capacity */
    const cxpr_ast* ast;        /**< Borrowed root AST for typed fallback evaluation */
    cxpr_ir_lookup_cache* lookup_cache; /**< Optional per-instruction lookup cache */
    unsigned char fast_result_kind; /**< 0=unknown, 1=double, 2=bool for scalar fast-path */
} cxpr_ir_program;

struct cxpr_program {
    cxpr_ir_program ir;
    const cxpr_ast* ast;
};

/** @brief One substitution frame used while inlining defined-function bodies into IR. */
typedef struct cxpr_ir_subst_frame {
    const char* const* names;
    const cxpr_ast* const* args;
    size_t count;
    const struct cxpr_ir_subst_frame* parent;
} cxpr_ir_subst_frame;

/** @brief Check whether a name maps to a builtin handled specially by IR compilation.
 * @param name Candidate function name.
 * @return true when the name is recognized as a special builtin.
 */
bool cxpr_ir_is_special_builtin_name(const char* name);
/** @brief Append one instruction to an IR program.
 * @param program Destination IR program.
 * @param instr Instruction to append.
 * @param err Optional error output when growth fails.
 * @return true on success, false on allocation failure.
 */
bool cxpr_ir_emit(cxpr_ir_program* program, cxpr_ir_instr instr, cxpr_error* err);
/** @brief Return the next instruction index for an IR program.
 * @param program Program to inspect.
 * @return Current instruction count, used as the next write index.
 */
size_t cxpr_ir_next_index(const cxpr_ir_program* program);
/** @brief Patch the jump target of an already-emitted instruction.
 * @param program Program containing the instruction.
 * @param at Instruction index to patch.
 * @param target Target instruction index to store.
 */
void cxpr_ir_patch_target(cxpr_ir_program* program, size_t at, size_t target);
/** @brief Try to fold an AST subtree to a compile-time constant.
 * @param ast AST subtree to inspect.
 * @param out Output location for the constant value.
 * @return true when the subtree is pure and was folded successfully.
 */
bool cxpr_ir_constant_value(const cxpr_ast* ast, double* out);
/** @brief Compare two AST subtrees structurally.
 * @param left Left subtree.
 * @param right Right subtree.
 * @return true when both subtrees have identical structure and payload.
 */
bool cxpr_ir_ast_equal(const cxpr_ast* left, const cxpr_ast* right);
/** @brief Populate an IR runtime error and return a NaN field sentinel.
 * @param err Optional error output to populate.
 * @param message Error message to attach.
 * @return A `cxpr_value` wrapping `NAN`.
 */
cxpr_value cxpr_ir_runtime_error(cxpr_error* err, const char* message);
/** @brief Push one value onto an IR operand stack.
 * @param stack Stack storage.
 * @param sp In/out stack pointer.
 * @param value Value to push.
 * @param capacity Maximum stack capacity.
 * @param err Optional error output on overflow.
 * @return true on success, false if the push would overflow.
 */
bool cxpr_ir_stack_push(cxpr_value* stack, size_t* sp, cxpr_value value,
                        size_t capacity, cxpr_error* err);
/** @brief Ensure that a stack contains at least a given number of values.
 * @param sp Current stack depth.
 * @param need Required minimum depth.
 * @param err Optional error output on underflow.
 * @return true when the stack depth is sufficient.
 */
bool cxpr_ir_require_stack(size_t sp, size_t need, cxpr_error* err);
/** @brief Validate that a field value has the expected runtime type.
 * @param value Value to inspect.
 * @param type Expected field type.
 * @param err Optional error output on mismatch.
 * @param message Error message to attach on mismatch.
 * @return true when `value.type` matches `type`.
 */
bool cxpr_ir_require_type(cxpr_value value, cxpr_value_type type,
                          cxpr_error* err, const char* message);
/** @brief Populate an unknown-identifier error and return a NaN field sentinel.
 * @param err Optional error output to populate.
 * @param message Error message to attach.
 * @return A `cxpr_value` wrapping `NAN`.
 */
cxpr_value cxpr_ir_make_not_found(cxpr_error* err, const char* message);
/** @brief Validate stack safety for the scalar fast-path executor.
 * @param program Compiled IR program to validate.
 * @return true when all reachable paths have consistent, safe stack depth.
 */
bool cxpr_ir_validate_scalar_fast_program(const cxpr_ir_program* program);
/** @brief Build a cache key for a producer call with constant AST arguments.
 * @param name Producer name.
 * @param args Producer argument AST nodes.
 * @param argc Number of arguments in `args`.
 * @return Newly allocated cache key, or NULL when the arguments are not constant.
 */
char* cxpr_ir_build_constant_producer_key(const char* name, const cxpr_ast* const* args,
                                          size_t argc);
/** @brief Resolve a scalar identifier lookup using the per-instruction cache when possible.
 * @param ctx Request context.
 * @param instr IR load instruction carrying the lookup name/hash.
 * @param cache Mutable cache entry for the instruction, or NULL.
 * @param param_lookup True to read from parameter storage instead of variables.
 * @param found Optional output set to true on success.
 * @return Resolved scalar value, or `0.0` when the identifier is missing.
 */
double cxpr_ir_lookup_cached_scalar(const cxpr_context* ctx, const cxpr_ir_instr* instr,
                                    cxpr_ir_lookup_cache* cache, bool param_lookup,
                                    bool* found);
/** @brief Build a producer-struct cache key from a function name and scalar arguments.
 * @param name Producer name.
 * @param args Scalar argument array.
 * @param argc Number of arguments in `args`.
 * @param local_buf Optional caller-provided buffer for small keys.
 * @param local_cap Capacity of `local_buf`.
 * @param heap_buf Optional output receiving a heap allocation when one is needed.
 * @return Pointer to the cache key string, or NULL on allocation/format failure.
 */
const char* cxpr_ir_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                           char* local_buf, size_t local_cap, char** heap_buf);
/** @brief Check whether a defined function can stay on the scalar-only IR path.
 * @param entry Function registry entry to inspect.
 * @return true when the defined function has no struct-valued parameters.
 */
bool cxpr_ir_defined_is_scalar_only(const cxpr_func_entry* entry);
/** @brief Release all storage owned by one internal IR program. */
void cxpr_ir_program_reset(cxpr_ir_program* program);
/** @brief Compile one AST into internal IR. */
bool cxpr_ir_compile(const cxpr_ast* ast, const cxpr_registry* reg,
                     cxpr_ir_program* program, cxpr_error* err);
/** @brief Compile one AST into IR with an explicit local-variable table. */
bool cxpr_ir_compile_with_locals(const cxpr_ast* ast, const cxpr_registry* reg,
                                 const char* const* local_names, size_t local_count,
                                 cxpr_ir_program* program, cxpr_error* err);
/** @brief Lazily compile the IR program backing one defined function. */
bool cxpr_ir_prepare_defined_program(cxpr_func_entry* entry, const cxpr_registry* reg,
                                     cxpr_error* err);
/** @brief Execute internal IR and return a scalar numeric result. */
double cxpr_ir_exec(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err);
/** @brief Execute internal IR with an explicit local-value array. */
double cxpr_ir_exec_with_locals(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err);
/** @brief Return a stable printable opcode name. */
const char* cxpr_ir_opcode_name(cxpr_opcode op);
void cxpr_program_free(cxpr_program* prog);

#endif
