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

#endif /* CXPR_CORE_H */
