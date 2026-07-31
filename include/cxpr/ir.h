/**
 * @file ir.h
 * @brief Read-only public view of compiled cxpr IR programs.
 */

#ifndef CXPR_IR_VIEW_H
#define CXPR_IR_VIEW_H

#include <cxpr/types.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CXPR_IR_VIEW_API_VERSION 1u

/**
 * @brief Stable public opcode tags for inspecting a compiled program.
 *
 * These values intentionally do not expose cxpr's internal IR structs. The
 * opcode sequence is a borrowed read-only view valid for the lifetime of the
 * `cxpr_expr_compiled` it came from. Opcode values are an inspection API, not a
 * serialized bytecode format; persist source or generated C instead.
 */
typedef enum {
    CXPR_IR_OP_UNKNOWN = 0,
    CXPR_IR_OP_PUSH_CONST,
    CXPR_IR_OP_PUSH_BOOL,
    CXPR_IR_OP_PUSH_STRING,
    CXPR_IR_OP_BUILD_ARRAY,
    CXPR_IR_OP_LOAD_LOCAL,
    CXPR_IR_OP_LOAD_LOCAL_SQUARE,
    CXPR_IR_OP_LOAD_VAR,
    CXPR_IR_OP_LOAD_VAR_SQUARE,
    CXPR_IR_OP_LOAD_PARAM,
    CXPR_IR_OP_LOAD_PARAM_SQUARE,
    CXPR_IR_OP_LOAD_FIELD,
    CXPR_IR_OP_LOAD_FIELD_SQUARE,
    CXPR_IR_OP_LOAD_NAMED_FIELD,
    CXPR_IR_OP_LOAD_CHAIN,
    CXPR_IR_OP_ADD,
    CXPR_IR_OP_SUB,
    CXPR_IR_OP_MUL,
    CXPR_IR_OP_SQUARE,
    CXPR_IR_OP_DIV,
    CXPR_IR_OP_MOD,
    CXPR_IR_OP_CMP_EQ,
    CXPR_IR_OP_CMP_NEQ,
    CXPR_IR_OP_CMP_LT,
    CXPR_IR_OP_CMP_LTE,
    CXPR_IR_OP_CMP_GT,
    CXPR_IR_OP_CMP_GTE,
    CXPR_IR_OP_NOT,
    CXPR_IR_OP_NEG,
    CXPR_IR_OP_SIGN,
    CXPR_IR_OP_SQRT,
    CXPR_IR_OP_ABS,
    CXPR_IR_OP_FLOOR,
    CXPR_IR_OP_CEIL,
    CXPR_IR_OP_ROUND,
    CXPR_IR_OP_POW,
    CXPR_IR_OP_CLAMP,
    CXPR_IR_OP_CALL_PRODUCER,
    CXPR_IR_OP_CALL_PRODUCER_CONST,
    CXPR_IR_OP_CALL_PRODUCER_CONST_FIELD,
    CXPR_IR_OP_GET_FIELD,
    CXPR_IR_OP_CALL_UNARY,
    CXPR_IR_OP_CALL_BINARY,
    CXPR_IR_OP_CALL_TERNARY,
    CXPR_IR_OP_CALL_FUNC,
    CXPR_IR_OP_CALL_DEFINED,
    CXPR_IR_OP_CALL_AST,
    CXPR_IR_OP_JUMP,
    CXPR_IR_OP_JUMP_IF_FALSE,
    CXPR_IR_OP_JUMP_IF_TRUE,
    CXPR_IR_OP_LOOKBACK_PUSH,
    CXPR_IR_OP_LOOKBACK_POP,
    CXPR_IR_OP_LOOKBACK_RESOLVE,
    CXPR_IR_OP_STORE_LOCAL,
    CXPR_IR_OP_RETURN,
    CXPR_IR_OP_INDEX
} cxpr_ir_opcode;

/** @brief Best-effort scalar result kind inferred for fast execution. */
typedef enum {
    CXPR_IR_RESULT_UNKNOWN = 0,
    CXPR_IR_RESULT_NUMBER = 1,
    CXPR_IR_RESULT_BOOL = 2
} cxpr_ir_result_kind;

/**
 * @brief Read-only public instruction view.
 *
 * String and array pointers are borrowed from the compiled program and must not
 * be freed or mutated by callers. Fields are populated only when the matching
 * `has_*` flag is true or the pointer is non-NULL.
 */
typedef struct {
    cxpr_ir_opcode op;
    const char* name;       /**< Opcode-specific symbol, field, or cache key. */
    const char* aux_name;   /**< Secondary symbol, typically a selected field. */
    const char* func_name;  /**< Registered function/producer name for call ops. */
    double value;           /**< PUSH_CONST or PUSH_BOOL payload. */
    size_t index;           /**< LOAD_LOCAL or jump target operand. */
    size_t arg_count;       /**< Function/producer argument count or array element count. */
    unsigned long hash;     /**< Cached lookup hash for load ops. */
    const double* number_args; /**< Constant numeric call args, when available. */
    size_t number_arg_count;
    bool has_value;
    bool has_index;
    bool has_arg_count;
    bool has_hash;
    bool has_number_args;
} cxpr_ir_instruction;

/**
 * @brief Return the number of IR instructions in a compiled program.
 * @param program Program to inspect.
 * @return Instruction count, or 0 for NULL.
 */
size_t cxpr_expr_compiled_ir_count(const cxpr_expr_compiled* program);

/**
 * @brief Copy one instruction into a public read-only view struct.
 * @param program Program to inspect.
 * @param index Zero-based instruction index.
 * @param out Output instruction view.
 * @return True when `out` was populated, false for NULL/out-of-range input.
 */
bool cxpr_expr_compiled_ir_instruction(const cxpr_expr_compiled* program,
                           size_t index,
                           cxpr_ir_instruction* out);

/**
 * @brief Return the inferred fast-result kind for a compiled program.
 * @param program Program to inspect.
 * @return Result kind, or UNKNOWN for NULL.
 */
cxpr_ir_result_kind cxpr_expr_compiled_ir_result_kind(const cxpr_expr_compiled* program);

/**
 * @brief Return a static readable name for a public IR opcode.
 * @param op Opcode to describe.
 * @return Static opcode name.
 */
const char* cxpr_ir_opcode_name(cxpr_ir_opcode op);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_IR_VIEW_H */
