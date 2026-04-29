/**
 * @file ir_cache_key.c
 * @brief IR producer cache-key helpers.
 */

#include "core.h"
#include "internal.h"

const char* cxpr_ir_build_struct_cache_key(const char* name, const double* args, size_t argc,
                                           char* local_buf, size_t local_cap, char** heap_buf) {
    size_t len;
    size_t offset;
    char* key;
    int written;

    if (heap_buf) *heap_buf = NULL;
    if (!name) return NULL;
    if (argc == 0) return name;

    len = strlen(name) + 4 + (argc * 32);
    if (local_buf && len <= local_cap) {
        key = local_buf;
    } else {
        key = (char*)malloc(len);
        if (!key) return NULL;
        if (heap_buf) *heap_buf = key;
    }

    written = snprintf(key, len, "%s(", name);
    if (written < 0 || (size_t)written >= len) {
        if (heap_buf && *heap_buf) free(*heap_buf);
        if (heap_buf) *heap_buf = NULL;
        return NULL;
    }
    offset = (size_t)written;

    for (size_t i = 0; i < argc; ++i) {
        written = snprintf(key + offset, len - offset, i == 0 ? "%a" : ",%a", args[i]);
        if (written < 0 || (size_t)written >= len - offset) {
            if (heap_buf && *heap_buf) free(*heap_buf);
            if (heap_buf) *heap_buf = NULL;
            return NULL;
        }
        offset += (size_t)written;
    }

    written = snprintf(key + offset, len - offset, ")");
    if (written < 0 || (size_t)written >= len - offset) {
        if (heap_buf && *heap_buf) free(*heap_buf);
        if (heap_buf) *heap_buf = NULL;
        return NULL;
    }

    return key;
}

char* cxpr_ir_build_constant_producer_key(const char* name, const cxpr_ast* const* args,
                                          size_t argc) {
    double values[CXPR_MAX_CALL_ARGS];
    char local_buf[256];
    char* heap_buf = NULL;
    const char* key;

    if (!name || argc > CXPR_MAX_CALL_ARGS) return NULL;
    for (size_t i = 0; i < argc; ++i) {
        if (!cxpr_ir_constant_value(args[i], &values[i])) return NULL;
    }

    key = cxpr_ir_build_struct_cache_key(name, values, argc, local_buf, sizeof(local_buf), &heap_buf);
    if (!key) return NULL;
    if (heap_buf) return heap_buf;
    return cxpr_strdup(key);
}
