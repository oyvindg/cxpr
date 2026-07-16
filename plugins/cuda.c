#include <cxpr/plugins/cuda.h>
#include <cxpr/model/model.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct cxpr_cuda_buf {
    char* data;
    size_t len;
    size_t cap;
} cxpr_cuda_buf;

static int cxpr_cuda_append_bytes(
    cxpr_cuda_buf* b,
    const char* text,
    size_t n) {
    if (!b || !text) return 0;
    if (b->len + n + 1u > b->cap) {
        size_t cap = b->cap ? b->cap : 512u;
        char* next;
        while (b->len + n + 1u > cap) cap *= 2u;
        next = (char*)realloc(b->data, cap);
        if (!next) return 0;
        b->data = next;
        b->cap = cap;
    }
    memcpy(b->data + b->len, text, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static int cxpr_cuda_append(cxpr_cuda_buf* b, const char* text) {
    return cxpr_cuda_append_bytes(b, text, strlen(text));
}

static char* cxpr_cuda_replace_all(
    const char* source,
    const char* needle,
    const char* replacement) {
    cxpr_cuda_buf out = {0};
    const size_t needle_len = strlen(needle);
    const char* p = source;

    if (!source || !needle || !replacement || needle_len == 0u) return NULL;
    while (*p) {
        const char* hit = strstr(p, needle);
        if (!hit) {
            if (!cxpr_cuda_append(&out, p)) {
                free(out.data);
                return NULL;
            }
            return out.data;
        }
        if (!cxpr_cuda_append_bytes(&out, p, (size_t)(hit - p)) ||
            !cxpr_cuda_append(&out, replacement)) {
            free(out.data);
            return NULL;
        }
        p = hit + needle_len;
    }
    if (!out.data && !cxpr_cuda_append(&out, "")) return NULL;
    return out.data;
}

static int cxpr_cuda_code_uses_call(const char* code, const char* name) {
    const size_t name_len = strlen(name);
    const char* p = code;

    while (p && *p) {
        const char* hit = strstr(p, name);
        const char* after;
        unsigned char before;

        if (!hit) return 0;
        before = hit == code ? 0u : (unsigned char)hit[-1];
        after = hit + name_len;
        if ((before == 0u || !(isalnum(before) || before == '_')) &&
            after[0] == '(') {
            return 1;
        }
        p = hit + name_len;
    }
    return 0;
}

static int cxpr_cuda_append_math_binding(
    cxpr_cuda_buf* out,
    const char* code,
    const char* name,
    const char* extern_decl,
    const char* macro_decl) {
    if (!cxpr_cuda_code_uses_call(code, name)) return 1;
    return cxpr_cuda_append(out, extern_decl) &&
           cxpr_cuda_append(out, macro_decl);
}

char* cxpr_cuda_plugin_source_from_program(
    const cxpr_model_program* program,
    const cxpr_cuda_plugin_options* options,
    cxpr_error* err) {
    static const cxpr_cuda_plugin_options defaults = {
        "cxpr_model_tick",
        "static __device__ __forceinline__"
    };
    const cxpr_cuda_plugin_options* opts = options ? options : &defaults;
    const char* function_name = opts->function_name ? opts->function_name : defaults.function_name;
    const char* qualifiers = opts->qualifiers ? opts->qualifiers : defaults.qualifiers;
    char* code;
    char* patched;
    char* cuda_patched;
    cxpr_cuda_buf out = {0};

    code = cxpr_model_program_to_c_tick_function(
        program,
        qualifiers,
        function_name,
        err);
    if (!code) return NULL;

    patched = cxpr_cuda_replace_all(
        code,
        "static inline double cxpr_fn_",
        "static __device__ __forceinline__ double cxpr_fn_");
    if (!patched) {
        free(code);
        return NULL;
    }
    cuda_patched = cxpr_cuda_replace_all(patched, " restrict ", " __restrict__ ");
    free(patched);
    if (!cuda_patched) {
        free(code);
        return NULL;
    }

    if (!cxpr_cuda_append_math_binding(
            &out,
            code,
            "fabs",
            "extern \"C\" __device__ double __nv_fabs(double);\n",
            "#define fabs(x) __nv_fabs((double)(x))\n") ||
        !cxpr_cuda_append_math_binding(
            &out,
            code,
            "floor",
            "extern \"C\" __device__ double __nv_floor(double);\n",
            "#define floor(x) __nv_floor((double)(x))\n") ||
        !cxpr_cuda_append_math_binding(
            &out,
            code,
            "fmax",
            "extern \"C\" __device__ double __nv_fmax(double, double);\n",
            "#define fmax(a, b) __nv_fmax((double)(a), (double)(b))\n") ||
        !cxpr_cuda_append_math_binding(
            &out,
            code,
            "fmin",
            "extern \"C\" __device__ double __nv_fmin(double, double);\n",
            "#define fmin(a, b) __nv_fmin((double)(a), (double)(b))\n") ||
        !cxpr_cuda_append_math_binding(
            &out,
            code,
            "round",
            "extern \"C\" __device__ double __nv_round(double);\n",
            "#define round(x) __nv_round((double)(x))\n") ||
        !cxpr_cuda_append_math_binding(
            &out,
            code,
            "sqrt",
            "extern \"C\" __device__ double __nv_sqrt(double);\n",
            "#define sqrt(x) __nv_sqrt((double)(x))\n") ||
        !cxpr_cuda_append(&out, "#ifndef CXPR_UNLIKELY\n"
                                "#define CXPR_UNLIKELY(x) (x)\n"
                                "#endif\n\n") ||
        !cxpr_cuda_append(&out, cuda_patched)) {
        free(out.data);
        out.data = NULL;
    }

    free(cuda_patched);
    free(code);
    return out.data;
}

int cxpr_cuda_plugin_emit_source(
    const cxpr_plugin_model_event* event,
    const cxpr_cuda_plugin_options* options,
    const cxpr_plugin_host* host,
    cxpr_error* err) {
    cxpr_plugin_artifact_event artifact = {
        "cxpr_cuda_source",
        "cxpr.cuda.source.v1",
        NULL
    };
    char* source;
    int ok;

    if (!event || !event->program || !host ||
        !host->begin_artifact || !host->write_artifact || !host->end_artifact) {
        return 0;
    }
    source = cxpr_cuda_plugin_source_from_program(event->program, options, err);
    if (!source) return 0;
    artifact.name = event->model_path ? event->model_path : artifact.name;
    ok = host->begin_artifact(host->user, &artifact, err) &&
         host->write_artifact(host->user, source, strlen(source), err) &&
         host->end_artifact(host->user, err);
    free(source);
    return ok ? 1 : 0;
}

void cxpr_cuda_plugin_source_free(char* source) {
    free(source);
}

static int cxpr_cuda_plugin_generate_bridge(
    const cxpr_plugin_model_event* event,
    const void* options,
    const cxpr_plugin_host* host,
    cxpr_error* err) {
    return cxpr_cuda_plugin_emit_source(
        event,
        (const cxpr_cuda_plugin_options*)options,
        host,
        err);
}

const cxpr_plugin_backend* cxpr_cuda_plugin_backend(void) {
    static const cxpr_plugin_backend backend = {
        "cxpr.cuda.source",
        cxpr_cuda_plugin_generate_bridge
    };
    return &backend;
}
