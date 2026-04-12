/**
 * @file types.h
 * @brief Core public types for the cxpr C API.
 */

#ifndef CXPR_TYPES_H
#define CXPR_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque parser handle. */
typedef struct cxpr_parser cxpr_parser;
/** @brief Opaque AST handle. */
typedef struct cxpr_ast cxpr_ast;
/** @brief Opaque evaluation context handle. */
typedef struct cxpr_context cxpr_context;
/** @brief Opaque function registry handle. */
typedef struct cxpr_registry cxpr_registry;
/** @brief Opaque compiled program handle. */
typedef struct cxpr_program cxpr_program;
/** @brief Opaque expression evaluator handle. */
typedef struct cxpr_evaluator cxpr_evaluator;
/** @brief Opaque/public struct-value handle. */
typedef struct cxpr_struct_value cxpr_struct_value;

/** @brief Error codes returned by cxpr APIs. */
typedef enum {
    CXPR_OK = 0,
    CXPR_ERR_SYNTAX,
    CXPR_ERR_UNKNOWN_IDENTIFIER,
    CXPR_ERR_UNKNOWN_FUNCTION,
    CXPR_ERR_WRONG_ARITY,
    CXPR_ERR_DIVISION_BY_ZERO,
    CXPR_ERR_CIRCULAR_DEPENDENCY,
    CXPR_ERR_TYPE_MISMATCH,
    CXPR_ERR_OUT_OF_MEMORY
} cxpr_error_code;

/** @brief Runtime type tags for `cxpr_value`. */
typedef enum {
    CXPR_VALUE_NUMBER = 0,
    CXPR_VALUE_BOOL = 1,
    CXPR_VALUE_STRUCT = 2
} cxpr_value_type;

/** @brief Typed runtime value used by evaluation and struct fields. */
typedef struct cxpr_value {
    cxpr_value_type type;
    union {
        double d;
        bool b;
        cxpr_struct_value* s;
    };
} cxpr_value;

/** @brief Owned collection of named typed fields. */
struct cxpr_struct_value {
    const char** field_names;
    cxpr_value* field_values;
    size_t field_count;
};

/**
 * @brief Construct a numeric `cxpr_value`.
 * @param d Numeric payload.
 * @return Value tagged as `CXPR_VALUE_NUMBER`.
 */
static inline cxpr_value cxpr_fv_double(double d) {
    return (cxpr_value){ .type = CXPR_VALUE_NUMBER, .d = d };
}

/**
 * @brief Construct a boolean `cxpr_value`.
 * @param b Boolean payload.
 * @return Value tagged as `CXPR_VALUE_BOOL`.
 */
static inline cxpr_value cxpr_fv_bool(bool b) {
    return (cxpr_value){ .type = CXPR_VALUE_BOOL, .b = b };
}

/**
 * @brief Construct a struct `cxpr_value`.
 * @param s Struct payload pointer.
 * @return Value tagged as `CXPR_VALUE_STRUCT`.
 */
static inline cxpr_value cxpr_fv_struct(cxpr_struct_value* s) {
    return (cxpr_value){ .type = CXPR_VALUE_STRUCT, .s = s };
}

/**
 * @brief Allocate a deep-copied struct value.
 * @param field_names Field-name array.
 * @param field_values Field-value array parallel to `field_names`.
 * @param field_count Number of fields to copy.
 * @return Newly allocated struct value, or NULL on allocation failure.
 */
cxpr_struct_value* cxpr_struct_value_new(const char* const* field_names,
                                         const cxpr_value* field_values,
                                         size_t field_count);
/**
 * @brief Free a struct value and any nested owned storage.
 * @param s Struct value to free. May be NULL.
 */
void cxpr_struct_value_free(cxpr_struct_value* s);

/** @brief Error payload filled by APIs that can fail. */
typedef struct cxpr_error {
    cxpr_error_code code;
    const char* message;
    size_t position;
    size_t line;
    size_t column;
} cxpr_error;

/**
 * @brief Return a human-readable string for an error code.
 * @param code Error code to describe.
 * @return Static string description for `code`.
 */
const char* cxpr_error_string(cxpr_error_code code);
/**
 * @brief Compute the internal key hash used by cxpr maps.
 * @param str NUL-terminated key string.
 * @return Hash value suitable for prehashed context APIs.
 */
unsigned long cxpr_hash_string(const char* str);

#ifdef __cplusplus
}
#endif

#endif /* CXPR_TYPES_H */
