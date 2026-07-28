#include "eval/snapshot/internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* cxpr_snapshot_strdup(const char* text) {
    size_t len;
    char* copy;

    if (!text) text = "";
    len = strlen(text);
    copy = (char*)malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

char* cxpr_snapshot_printf_number(double value) {
    char buf[64];

    if (isnan(value)) return cxpr_snapshot_strdup("nan");
    if (isinf(value)) return cxpr_snapshot_strdup(value < 0.0 ? "-inf" : "inf");
    snprintf(buf, sizeof(buf), "%.12g", value);
    return cxpr_snapshot_strdup(buf);
}

char* cxpr_snapshot_value_text(const cxpr_value* value);

static int cxpr_snapshot_append_text(char** buf, size_t* len, size_t* cap, const char* text) {
    size_t text_len;
    size_t needed;
    char* grown;

    if (!buf || !len || !cap) return 0;
    if (!text) text = "";
    text_len = strlen(text);
    needed = *len + text_len + 1u;
    if (needed > *cap) {
        size_t next = *cap ? *cap : 64u;
        while (next < needed) {
            if (next > (size_t)-1 / 2u) {
                next = needed;
                break;
            }
            next *= 2u;
        }
        grown = (char*)realloc(*buf, next);
        if (!grown) return 0;
        *buf = grown;
        *cap = next;
    }
    memcpy(*buf + *len, text, text_len);
    *len += text_len;
    (*buf)[*len] = '\0';
    return 1;
}

static char* cxpr_snapshot_array_text(const cxpr_array_value* array) {
    char* out = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    size_t shown;

    if (!array) return cxpr_snapshot_strdup("[]");
    shown = array->count < 6u ? array->count : 6u;
    if (!cxpr_snapshot_append_text(&out, &len, &cap, "[")) return NULL;
    for (size_t i = 0; i < shown; ++i) {
        char* item = cxpr_snapshot_value_text(&array->values[i]);
        if (!item) {
            free(out);
            return NULL;
        }
        if (i > 0u && !cxpr_snapshot_append_text(&out, &len, &cap, ", ")) {
            free(item);
            free(out);
            return NULL;
        }
        if (!cxpr_snapshot_append_text(&out, &len, &cap, item)) {
            free(item);
            free(out);
            return NULL;
        }
        free(item);
    }
    if (array->count > shown) {
        if (shown > 0u && !cxpr_snapshot_append_text(&out, &len, &cap, ", ")) {
            free(out);
            return NULL;
        }
        if (!cxpr_snapshot_append_text(&out, &len, &cap, "...")) {
            free(out);
            return NULL;
        }
    }
    if (!cxpr_snapshot_append_text(&out, &len, &cap, "]")) {
        free(out);
        return NULL;
    }
    return out;
}

static char* cxpr_snapshot_struct_text(const cxpr_struct_value* value) {
    char* out = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    size_t shown;

    if (!value) return cxpr_snapshot_strdup("{}");
    shown = value->field_count < 6u ? value->field_count : 6u;
    if (!cxpr_snapshot_append_text(&out, &len, &cap, "{")) return NULL;
    for (size_t i = 0; i < shown; ++i) {
        char* field_value = cxpr_snapshot_value_text(&value->field_values[i]);
        if (!field_value) {
            free(out);
            return NULL;
        }
        if (i > 0u && !cxpr_snapshot_append_text(&out, &len, &cap, ", ")) {
            free(field_value);
            free(out);
            return NULL;
        }
        if (!cxpr_snapshot_append_text(&out, &len, &cap,
                                       value->field_names[i] ? value->field_names[i] : "") ||
            !cxpr_snapshot_append_text(&out, &len, &cap, ": ") ||
            !cxpr_snapshot_append_text(&out, &len, &cap, field_value)) {
            free(field_value);
            free(out);
            return NULL;
        }
        free(field_value);
    }
    if (value->field_count > shown) {
        if (shown > 0u && !cxpr_snapshot_append_text(&out, &len, &cap, ", ")) {
            free(out);
            return NULL;
        }
        if (!cxpr_snapshot_append_text(&out, &len, &cap, "...")) {
            free(out);
            return NULL;
        }
    }
    if (!cxpr_snapshot_append_text(&out, &len, &cap, "}")) {
        free(out);
        return NULL;
    }
    return out;
}

char* cxpr_snapshot_value_text(const cxpr_value* value) {
    char buf[64];

    if (!value) return NULL;
    switch (value->type) {
        case CXPR_VALUE_NUMBER:
            return cxpr_snapshot_printf_number(value->d);
        case CXPR_VALUE_BOOL:
            return cxpr_snapshot_strdup(value->b ? "true" : "false");
        case CXPR_VALUE_STRING:
            return cxpr_snapshot_strdup(value->str ? value->str : "");
        case CXPR_VALUE_NULL:
            return cxpr_snapshot_strdup("null");
        case CXPR_VALUE_TIMESTAMP:
        case CXPR_VALUE_DURATION:
            snprintf(buf, sizeof(buf), "%lld", (long long)value->i64);
            return cxpr_snapshot_strdup(buf);
        case CXPR_VALUE_STRUCT:
            return cxpr_snapshot_struct_text(value->s);
        case CXPR_VALUE_ARRAY:
            return cxpr_snapshot_array_text(value->a);
        default:
            return cxpr_snapshot_strdup("{value}");
    }
}

cxpr_snapshot_state cxpr_snapshot_state_for_value(const cxpr_value* value) {
    if (!value) return CXPR_SNAPSHOT_STATE_UNKNOWN;
    if (value->type == CXPR_VALUE_BOOL) {
        return value->b ? CXPR_SNAPSHOT_STATE_TRUE : CXPR_SNAPSHOT_STATE_FALSE;
    }
    if (value->type == CXPR_VALUE_NUMBER) return CXPR_SNAPSHOT_STATE_NUMBER;
    return CXPR_SNAPSHOT_STATE_VALUE;
}

bool cxpr_snapshot_value_clone_failed(const cxpr_value* source,
                                      const cxpr_value* clone) {
    if (!source || !clone) return false;
    if (source->type == CXPR_VALUE_STRUCT && source->s && !clone->s) return true;
    if (source->type == CXPR_VALUE_STRING && source->str && !clone->str) return true;
    if (source->type == CXPR_VALUE_ARRAY && source->a && !clone->a) return true;
    return false;
}

const char* cxpr_snapshot_state_name(cxpr_snapshot_state state) {
    switch (state) {
        case CXPR_SNAPSHOT_STATE_TRUE: return "true";
        case CXPR_SNAPSHOT_STATE_FALSE: return "false";
        case CXPR_SNAPSHOT_STATE_NUMBER: return "number";
        case CXPR_SNAPSHOT_STATE_VALUE: return "value";
        case CXPR_SNAPSHOT_STATE_SKIPPED: return "skipped";
        case CXPR_SNAPSHOT_STATE_ERROR: return "error";
        default: return "unknown";
    }
}
