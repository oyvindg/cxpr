/**
 * @file types.h
 * @brief Core public types for the cxpr C API.
 */

#ifndef CXPR_TYPES_H
#define CXPR_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Number of elements in a fixed-size C array. */
#define CXPR_ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque parser handle. */
typedef struct cxpr_expr_parser cxpr_expr_parser;
/** @brief Opaque AST handle. */
typedef struct cxpr_expr_ast cxpr_expr_ast;
/** @brief Opaque evaluation context handle. */
typedef struct cxpr_context cxpr_context;
/** @brief Opaque function registry handle. */
typedef struct cxpr_registry cxpr_registry;
/** @brief Opaque compiled program handle. */
typedef struct cxpr_expr_compiled cxpr_expr_compiled;
/** @brief Opaque parsed .cxpr model handle. */
typedef struct cxpr_model cxpr_model;
/** @brief Opaque compiled .cxpr model program handle. */
typedef struct cxpr_model_compiled cxpr_model_compiled;
/** @brief Opaque mutable .cxpr model session handle. */
typedef struct cxpr_model_session cxpr_model_session;
/** @brief Opaque expression evaluator handle. */
typedef struct cxpr_evaluator cxpr_evaluator;
/** @brief Opaque/public struct-value handle. */
typedef struct cxpr_struct_value cxpr_struct_value;
/** @brief Opaque/public array-value handle. */
typedef struct cxpr_array_value cxpr_array_value;

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
    CXPR_ERR_OUT_OF_MEMORY,
    CXPR_ERR_INVALID_INDEX,
    CXPR_ERR_INDEX_OUT_OF_RANGE
} cxpr_error_code;

/** @brief Runtime type tags for `cxpr_value`. */
typedef enum {
    CXPR_VALUE_NUMBER = 0,
    CXPR_VALUE_BOOL = 1,
    CXPR_VALUE_STRUCT = 2,
    CXPR_VALUE_STRING = 3,
    CXPR_VALUE_NULL = 4,
    CXPR_VALUE_TIMESTAMP = 5,
    CXPR_VALUE_DURATION = 6,
    CXPR_VALUE_ARRAY = 7
} cxpr_value_type;

/** @brief Typed runtime value used by evaluation and struct fields. */
typedef struct cxpr_value {
    cxpr_value_type type;
    union {
        double d;
        bool b;
        cxpr_struct_value* s;
        const char* str;
        int64_t i64;
        cxpr_array_value* a;
    };
} cxpr_value;

/** @brief Owned collection of named typed fields. */
struct cxpr_struct_value {
    const char** field_names;
    cxpr_value* field_values;
    size_t field_count;
};

/** @brief Owned ordered collection of typed values. */
struct cxpr_array_value {
    cxpr_value* values;
    size_t count;
};

/**
 * @brief Construct a numeric `cxpr_value`.
 * @param d Numeric payload.
 * @return Value tagged as `CXPR_VALUE_NUMBER`.
 */
static inline cxpr_value cxpr_num(double d) {
    return (cxpr_value){ .type = CXPR_VALUE_NUMBER, .d = d };
}

/**
 * @brief Construct a boolean `cxpr_value`.
 * @param b Boolean payload.
 * @return Value tagged as `CXPR_VALUE_BOOL`.
 */
static inline cxpr_value cxpr_bool(bool b) {
    return (cxpr_value){ .type = CXPR_VALUE_BOOL, .b = b };
}

/**
 * @brief Construct a struct `cxpr_value`.
 * @param s Struct payload pointer.
 * @return Value tagged as `CXPR_VALUE_STRUCT`.
 */
static inline cxpr_value cxpr_struct(cxpr_struct_value* s) {
    return (cxpr_value){ .type = CXPR_VALUE_STRUCT, .s = s };
}

/**
 * @brief Construct a string `cxpr_value`.
 * @param str String payload pointer. The pointer is borrowed by the value.
 * @return Value tagged as `CXPR_VALUE_STRING`.
 */
static inline cxpr_value cxpr_string(const char* str) {
    return (cxpr_value){ .type = CXPR_VALUE_STRING, .str = str ? str : "" };
}

/**
 * @brief Construct a null `cxpr_value`.
 * @return Value tagged as `CXPR_VALUE_NULL`.
 */
static inline cxpr_value cxpr_null(void) {
    return (cxpr_value){ .type = CXPR_VALUE_NULL, .i64 = 0 };
}

/**
 * @brief Construct a timestamp `cxpr_value`.
 * @param unix_ns Timestamp payload as Unix nanoseconds.
 * @return Value tagged as `CXPR_VALUE_TIMESTAMP`.
 */
static inline cxpr_value cxpr_timestamp(int64_t unix_ns) {
    return (cxpr_value){ .type = CXPR_VALUE_TIMESTAMP, .i64 = unix_ns };
}

/**
 * @brief Construct a duration `cxpr_value`.
 * @param nanoseconds Duration payload in nanoseconds.
 * @return Value tagged as `CXPR_VALUE_DURATION`.
 */
static inline cxpr_value cxpr_duration(int64_t nanoseconds) {
    return (cxpr_value){ .type = CXPR_VALUE_DURATION, .i64 = nanoseconds };
}

/**
 * @brief Construct an array `cxpr_value`.
 * @param a Array payload pointer.
 * @return Value tagged as `CXPR_VALUE_ARRAY`.
 */
static inline cxpr_value cxpr_array(cxpr_array_value* a) {
    return (cxpr_value){ .type = CXPR_VALUE_ARRAY, .a = a };
}

/**
 * @brief Deep-clone a typed value.
 *
 * Strings are copied and owned by the returned value. Structs are deep-copied.
 * Use `cxpr_value_free` on cloned values that may own nested storage.
 *
 * @param value Source value.
 * @return Deep copy, or zero-like value on allocation failure.
 */
cxpr_value cxpr_value_clone(const cxpr_value* value);
/**
 * @brief Free storage owned by a typed value and reset it to zero.
 * @param value Value to clear. May be NULL.
 */
void cxpr_value_free(cxpr_value* value);

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

/**
 * @brief Allocate a deep-copied array value.
 * @param values Value array to copy.
 * @param count Number of values.
 * @return Newly allocated array value, or NULL on allocation failure.
 */
cxpr_array_value* cxpr_array_value_new(const cxpr_value* values, size_t count);
/**
 * @brief Free an array value and any nested owned storage.
 * @param a Array value to free. May be NULL.
 */
void cxpr_array_value_free(cxpr_array_value* a);

/**
 * @brief Error payload filled by APIs that can fail.
 *
 * `message` is always owned by `cxpr` — it points either to a static string
 * literal or to a thread-local scratch buffer that is overwritten by the next
 * failing call on the same thread. It is never heap-allocated; never `free` it.
 * Treat it as valid only until the next `cxpr` call on that thread, and copy it
 * (or use `cxpr_error_format`) if you need to retain it.
 */
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
 * @brief Format a complete, human-readable description of an error.
 *
 * Writes `"<code> at <line>:<column>: <message>"` (omitting the position when
 * the error carries none) into `buffer`, always NUL-terminating when `size` is
 * non-zero. The output is self-contained and safe to retain, unlike the
 * borrowed `cxpr_error.message` pointer.
 *
 * @param err Error to describe. A NULL or `CXPR_OK` error yields "no error".
 * @param buffer Destination buffer. May be NULL only when `size` is 0.
 * @param size Capacity of `buffer` in bytes.
 * @return Number of characters that the full description would occupy,
 *         excluding the NUL terminator (as `snprintf` does), so a return value
 *         `>= size` indicates truncation.
 */
size_t cxpr_error_format(const cxpr_error* err, char* buffer, size_t size);
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
