#ifndef CXPR_IR_INTERNAL_H
#define CXPR_IR_INTERNAL_H

#include "../internal.h"

#define CXPR_IR_INLINE_DEPTH_LIMIT 8
#define CXPR_IR_INFER_DEPTH_LIMIT 32
#define CXPR_IR_RESULT_UNKNOWN 0
#define CXPR_IR_RESULT_DOUBLE 1
#define CXPR_IR_RESULT_BOOL 2
#define CXPR_IR_STACK_CAPACITY 64

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
 * @return A `cxpr_field_value` wrapping `NAN`.
 */
cxpr_field_value cxpr_ir_runtime_error(cxpr_error* err, const char* message);
/** @brief Push one value onto an IR operand stack.
 * @param stack Stack storage.
 * @param sp In/out stack pointer.
 * @param value Value to push.
 * @param capacity Maximum stack capacity.
 * @param err Optional error output on overflow.
 * @return true on success, false if the push would overflow.
 */
bool cxpr_ir_stack_push(cxpr_field_value* stack, size_t* sp, cxpr_field_value value,
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
bool cxpr_ir_require_type(cxpr_field_value value, cxpr_field_type type,
                          cxpr_error* err, const char* message);
/** @brief Populate an unknown-identifier error and return a NaN field sentinel.
 * @param err Optional error output to populate.
 * @param message Error message to attach.
 * @return A `cxpr_field_value` wrapping `NAN`.
 */
cxpr_field_value cxpr_ir_make_not_found(cxpr_error* err, const char* message);
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

#endif
