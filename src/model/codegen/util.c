#include "model/codegen/internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cxpr_model_c_reserve(cxpr_model_c_buf* b, size_t extra) {
    if (!b || b->oom) return;
    if (b->len + extra + 1u > b->cap) {
        size_t cap = b->cap ? b->cap : 512u;
        char* grown;
        while (b->len + extra + 1u > cap) cap *= 2u;
        grown = (char*)realloc(b->data, cap);
        if (!grown) {
            b->oom = true;
            return;
        }
        b->data = grown;
        b->cap = cap;
    }
}

void cxpr_model_c_puts(cxpr_model_c_buf* b, const char* s) {
    size_t n;
    if (!b || !s) return;
    n = strlen(s);
    cxpr_model_c_reserve(b, n);
    if (b->oom) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void cxpr_model_c_printf(cxpr_model_c_buf* b, const char* fmt, ...) {
    va_list ap;
    va_list ap_copy;
    int needed;
    if (!b || b->oom || !fmt) return;
    va_start(ap, fmt);
    va_copy(ap_copy, ap);
    needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) {
        b->oom = true;
        va_end(ap_copy);
        return;
    }
    cxpr_model_c_reserve(b, (size_t)needed);
    if (!b->oom) {
        vsnprintf(b->data + b->len, b->cap - b->len, fmt, ap_copy);
        b->len += (size_t)needed;
    }
    va_end(ap_copy);
}

void cxpr_model_c_format_double(char* out, size_t out_size, double value) {
    if (isfinite(value) && floor(value) == value) {
        snprintf(out, out_size, "%.1f", value);
    } else {
        snprintf(out, out_size, "%.17g", value);
    }
}

char* cxpr_model_c_safe_name(const char* name) {
    size_t len = name ? strlen(name) : 0u;
    char* out = (char*)malloc(len + 4u);
    size_t pos = 0u;
    if (!out) return NULL;
    if (len == 0u || (name[0] >= '0' && name[0] <= '9')) {
        memcpy(out, "cx_", 3u);
        pos = 3u;
    }
    for (size_t i = 0u; i < len; ++i) {
        unsigned char ch = (unsigned char)name[i];
        out[pos++] = ((ch >= 'a' && ch <= 'z') ||
                      (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') ||
                      ch == '_') ? (char)ch : '_';
    }
    out[pos] = '\0';
    return out;
}

char* cxpr_model_c_function_name(const char* name) {
    char raw[512];
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "cxpr_fn_%s", name) >= sizeof(raw)) return NULL;
    return cxpr_model_c_safe_name(raw);
}

char* cxpr_model_c_scoped_function_name(const char* scope, const char* name) {
    char raw[768];
    if (!scope || !scope[0]) return cxpr_model_c_function_name(name);
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "cxpr_fn_%s_%s", scope, name) >= sizeof(raw)) {
        return NULL;
    }
    return cxpr_model_c_safe_name(raw);
}

char* cxpr_model_c_prefixed_name(const char* prefix, const char* name) {
    char raw[512];
    if (!name) return NULL;
    if ((size_t)snprintf(raw, sizeof(raw), "%s%s", prefix ? prefix : "", name) >= sizeof(raw)) {
        return NULL;
    }
    return cxpr_model_c_safe_name(raw);
}

unsigned long cxpr_model_c_name_hash(const char* s) {
    unsigned long h = 5381u;
    if (!s) return h;
    while (*s) h = ((h << 5u) + h) ^ (unsigned char)*s++;
    return h;
}

char* cxpr_model_c_child_tick_name(const char* function_prefix, size_t child_index) {
    char raw[128];
    snprintf(raw, sizeof(raw), "cxpr_c%08lx_%zu_tick",
             cxpr_model_c_name_hash(function_prefix) & 0xfffffffful,
             child_index);
    return cxpr_model_c_safe_name(raw);
}

char* cxpr_model_c_child_field_name(const char* function_prefix,
                                    size_t child_index,
                                    size_t field_index) {
    char raw[128];
    snprintf(raw, sizeof(raw), "cxpr_c%08lx_%zu_f%zu",
             cxpr_model_c_name_hash(function_prefix) & 0xfffffffful,
             child_index,
             field_index);
    return cxpr_model_c_safe_name(raw);
}

bool cxpr_model_c_is_power_of_two(size_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

bool cxpr_model_c_history_use_shift(size_t depth) {
    return depth <= 4u;
}

size_t cxpr_model_c_history_capacity(size_t depth) {
    size_t capacity = 1u;
    if (cxpr_model_c_history_use_shift(depth)) return depth;
    while (capacity < depth && capacity <= ((size_t)-1) / 2u) capacity *= 2u;
    return capacity < depth ? depth : capacity;
}
