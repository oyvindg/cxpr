/**
 * @file builtin.h
 * @brief Canonical metadata catalog for cxpr language builtins.
 */

#ifndef CXPR_BUILTIN_H
#define CXPR_BUILTIN_H

#include <cxpr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Builtin families and presentation flags. */
typedef enum cxpr_builtin_flags {
    CXPR_BUILTIN_DEFAULT = 1u << 0,
    CXPR_BUILTIN_TIMESERIES = 1u << 1,
    CXPR_BUILTIN_BASKET = 1u << 2,
    CXPR_BUILTIN_SYNTAX = 1u << 3,
    CXPR_BUILTIN_INTERNAL = 1u << 4
} cxpr_builtin_flags;

/** Language-facing metadata for one builtin function or syntax form. */
typedef struct cxpr_function_info {
    const char* name;
    const char* signature;
    const char* documentation;
    const char* const* param_names;
    size_t param_count;
    size_t min_args;
    size_t max_args;
    cxpr_value_type return_type;
    unsigned flags;
} cxpr_function_info;

/** @brief Return the number of entries in the canonical builtin catalog. */
size_t cxpr_builtin_count(void);

/** @brief Return builtin metadata by catalog index, or NULL when out of range. */
const cxpr_function_info* cxpr_builtin_at(size_t index);

/** @brief Find builtin metadata by exact language-facing name. */
const cxpr_function_info* cxpr_builtin_find(const char* name);

#ifdef __cplusplus
}
#endif

#endif
