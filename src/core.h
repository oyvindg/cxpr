/**
 * @file core.h
 * @brief Shared low-level helpers for cxpr internal modules.
 */

#ifndef CXPR_CORE_H
#define CXPR_CORE_H

#include <cxpr/cxpr.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Thread-local storage qualifier for cxpr's internal per-thread state.
 *
 * cxpr keeps two pieces of mutable state outside of caller-owned handles: the
 * empty-overlay reuse cache and the scratch buffers used to format error
 * messages. Both are made thread-local so that independent threads, each using
 * their own registry/context/program, never race on them. See the Concurrency
 * section of the README for the full threading contract.
 */
#if defined(_MSC_VER)
#define CXPR_THREAD_LOCAL __declspec(thread)
#else
#define CXPR_THREAD_LOCAL _Thread_local
#endif

/**
 * @brief Duplicate a NUL-terminated string using cxpr's allocator conventions.
 * @param s Source string to copy.
 * @return Newly allocated copy, or NULL when `s` is NULL or allocation fails.
 */
static inline char* cxpr_strdup(const char* s) {
    size_t len;
    char* copy;

    if (!s) return NULL;
    len = strlen(s) + 1;
    copy = (char*)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    return copy;
}

/**
 * @brief Portable `strtok_r`-style tokenizer used by cxpr internals.
 * @param str Input string on the first call, then NULL on subsequent calls.
 * @param delim Delimiter character set.
 * @param saveptr In/out cursor preserved between calls.
 * @return Pointer to the next token in `str`, or NULL when no tokens remain.
 */
static inline char* cxpr_strtok_r(char* str, const char* delim, char** saveptr) {
    char* start;
    char* end;

    if (!delim || !saveptr) return NULL;

    start = str ? str : *saveptr;
    if (!start) return NULL;

    start += strspn(start, delim);
    if (*start == '\0') {
        *saveptr = start;
        return NULL;
    }

    end = start + strcspn(start, delim);
    if (*end == '\0') {
        *saveptr = end;
    } else {
        *end = '\0';
        *saveptr = end + 1;
    }
    return start;
}

/**
 * @brief Normalized binary operator kind for the typed temporal value algebra.
 *
 * Shared by the tree evaluator and the typed IR executor so that timestamp and
 * duration arithmetic/ordering can never diverge between the two engines.
 */
typedef cxpr_numeric_op cxpr_valop;
#define CXPR_VALOP_ADD CXPR_NUMERIC_ADD
#define CXPR_VALOP_SUB CXPR_NUMERIC_SUB
#define CXPR_VALOP_MUL CXPR_NUMERIC_MUL
#define CXPR_VALOP_DIV CXPR_NUMERIC_DIV
#define CXPR_VALOP_LT CXPR_NUMERIC_LT
#define CXPR_VALOP_LTE CXPR_NUMERIC_LTE
#define CXPR_VALOP_GT CXPR_NUMERIC_GT
#define CXPR_VALOP_GTE CXPR_NUMERIC_GTE

/**
 * @brief Apply timestamp/duration arithmetic and ordering to typed operands.
 *
 * Implements the closed temporal algebra:
 *   - `timestamp - timestamp -> duration`
 *   - `timestamp +/- duration -> timestamp`, `duration + timestamp -> timestamp`
 *   - `duration +/- duration -> duration`
 *   - `duration * number -> duration`, `number * duration -> duration`
 *   - `duration / number -> duration`, `duration / duration -> number`
 *   - ordering (`< <= > >=`) on two timestamps or two durations -> bool
 *
 * Also implements record/struct arithmetic:
 *   - struct plus/minus/times/divide struct -> struct, fieldwise when both operands have the
 *     same named fields
 *   - struct plus/minus/times/divide number -> struct and
 *     number plus/minus/times/divide struct -> struct
 *     fieldwise, preserving operand order for non-commutative operators
 *
 * @param op Normalized operator.
 * @param a Left operand.
 * @param b Right operand.
 * @param out Result on success.
 * @param err Optional error output (set on type mismatch / division by zero).
 * @return 1 when handled (`out` written), 0 when neither operand is a
 *         timestamp/duration (caller applies its own numeric path), and -1 on
 *         error (`err` set).
 */
int cxpr_value_binary_op(cxpr_valop op, cxpr_value a, cxpr_value b,
                         cxpr_value* out, cxpr_error* err);

/** Checked numeric algebra shared by AST and IR execution. */
bool cxpr_numeric_equal(cxpr_value a, cxpr_value b);

#endif /* CXPR_CORE_H */
