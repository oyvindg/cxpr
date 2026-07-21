/**
 * @file expression/params.c
 * @brief Source-level parameter rewrite helpers.
 */

#include <cxpr/expression.h>

#include <stdlib.h>
#include <string.h>

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} cxpr_expression_strbuf;

static int cxpr_expression_strbuf_grow(cxpr_expression_strbuf* b, size_t extra) {
    size_t need;
    size_t cap;
    char* grown;

    if (!b) return 0;
    if (extra > (size_t)-1 - b->len - 1u) return 0;
    need = b->len + extra + 1u;
    if (need <= b->cap) return 1;
    cap = b->cap ? b->cap * 2u : 128u;
    while (cap < need) {
        if (cap > (size_t)-1 / 2u) {
            cap = need;
            break;
        }
        cap *= 2u;
    }
    grown = (char*)realloc(b->data, cap);
    if (!grown) return 0;
    b->data = grown;
    b->cap = cap;
    return 1;
}

static int cxpr_expression_strbuf_append_n(
    cxpr_expression_strbuf* b,
    const char* s,
    size_t n) {
    if (!b || (!s && n > 0u)) return 0;
    if (!cxpr_expression_strbuf_grow(b, n)) return 0;
    if (n > 0u) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static int cxpr_expression_strbuf_append(cxpr_expression_strbuf* b, const char* s) {
    return s ? cxpr_expression_strbuf_append_n(b, s, strlen(s)) : 0;
}

static int cxpr_expression_strbuf_append_ch(cxpr_expression_strbuf* b, char ch) {
    return cxpr_expression_strbuf_append_n(b, &ch, 1u);
}

static char* cxpr_expression_strdup(const char* text) {
    char* out;
    size_t len;

    if (!text) return NULL;
    len = strlen(text);
    out = (char*)malloc(len + 1u);
    if (!out) return NULL;
    memcpy(out, text, len + 1u);
    return out;
}

static int cxpr_expression_is_ident_start(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           ch == '_';
}

static int cxpr_expression_is_ident_part(char ch) {
    return cxpr_expression_is_ident_start(ch) || (ch >= '0' && ch <= '9');
}

static const char* cxpr_expression_scan_dotted_ident(const char* start) {
    const char* q;

    if (!start || !cxpr_expression_is_ident_start(*start)) return start;
    q = start + 1;
    while (*q) {
        if (cxpr_expression_is_ident_part(*q)) {
            ++q;
            continue;
        }
        if (*q == '.' && cxpr_expression_is_ident_start(q[1])) {
            q += 2;
            while (cxpr_expression_is_ident_part(*q)) ++q;
            continue;
        }
        break;
    }
    return q;
}

char* cxpr_expression_inline_params(const char* expression,
                                    cxpr_expression_param_lookup_fn lookup,
                                    void* userdata) {
    cxpr_expression_strbuf out = {0};
    const char* p;
    int changed = 0;

    if (!expression || !lookup) return NULL;
    for (p = expression; *p;) {
        if (*p == '"' || *p == '\'') {
            const char quote = *p++;
            if (!cxpr_expression_strbuf_append_ch(&out, quote)) goto fail;
            while (*p) {
                const char ch = *p++;
                if (!cxpr_expression_strbuf_append_ch(&out, ch)) goto fail;
                if (ch == quote) break;
                if (ch == '\\' && *p) {
                    if (!cxpr_expression_strbuf_append_ch(&out, *p++)) goto fail;
                }
            }
            continue;
        }

        if (*p == '$' && cxpr_expression_is_ident_start(p[1])) {
            const char* q = cxpr_expression_scan_dotted_ident(p + 1);
            char key[512];
            const char* value = NULL;
            size_t key_len = (size_t)(q - (p + 1));

            if (key_len < sizeof(key)) {
                memcpy(key, p + 1, key_len);
                key[key_len] = '\0';
                value = lookup(key, userdata);
            }
            if (value) {
                if (!cxpr_expression_strbuf_append(&out, value)) goto fail;
                changed = 1;
            } else if (!cxpr_expression_strbuf_append_n(&out, p, (size_t)(q - p))) {
                goto fail;
            }
            p = q;
            continue;
        }

        if (!cxpr_expression_strbuf_append_ch(&out, *p++)) goto fail;
    }

    if (!out.data) return cxpr_expression_strdup(expression);
    if (!changed) {
        free(out.data);
        return cxpr_expression_strdup(expression);
    }
    return out.data;

fail:
    free(out.data);
    return NULL;
}
