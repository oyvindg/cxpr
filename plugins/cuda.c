#include <cxpr/plugins/cuda.h>
#include <cxpr/model/model.h>

#include <ctype.h>
#include <stdio.h>
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

static char* cxpr_cuda_inline_init_state(
    const char* source,
    const char* function_name) {
    cxpr_cuda_buf without_init = {0};
    cxpr_cuda_buf replacement = {0};
    char* marker;
    char* call;
    char* out;
    const char* marker_start;
    const char* func_open;
    const char* func_close = NULL;
    const char* after_func;
    const char* body_start;
    const char* p;
    int depth = 0;
    size_t marker_len;
    size_t call_len;

    if (!source || !function_name || function_name[0] == '\0') return NULL;
    marker_len = strlen("/* Source model slot init:  */\n") + strlen(function_name) + 1u;
    marker = (char*)malloc(marker_len);
    if (!marker) return NULL;
    snprintf(marker, marker_len, "/* Source model slot init: %s */\n", function_name);
    marker_start = strstr(source, marker);
    free(marker);
    if (!marker_start) {
        if (!cxpr_cuda_append(&without_init, source)) return NULL;
        return without_init.data;
    }

    func_open = strchr(marker_start, '{');
    if (!func_open) goto unchanged;
    for (p = func_open; *p; ++p) {
        if (*p == '{') depth++;
        else if (*p == '}') {
            depth--;
            if (depth == 0) {
                func_close = p;
                break;
            }
        }
    }
    if (depth != 0 || !func_close) goto unchanged;
    after_func = func_close + 1;
    if (*after_func == '\n') after_func++;
    if (*after_func == '\n') after_func++;

    call_len = strlen("    if (CXPR_UNLIKELY(_cx_state->init == 0u)) _init_state(_cx_state);\n") +
               strlen(function_name) + 1u;
    call = (char*)malloc(call_len);
    if (!call) return NULL;
    snprintf(
        call,
        call_len,
        "    if (CXPR_UNLIKELY(_cx_state->init == 0u)) %s_init_state(_cx_state);\n",
        function_name);
    if (!strstr(after_func, call)) {
        free(call);
        goto unchanged;
    }

    if (!cxpr_cuda_append_bytes(&without_init, source, (size_t)(marker_start - source)) ||
        !cxpr_cuda_append(&without_init, after_func)) {
        free(call);
        free(without_init.data);
        return NULL;
    }

    if (!cxpr_cuda_append(&replacement, "    if (CXPR_UNLIKELY(_cx_state->init == 0u)) {\n")) {
        free(call);
        free(without_init.data);
        return NULL;
    }
    body_start = func_open + 1;
    if (*body_start == '\n') body_start++;
    if (body_start > func_close ||
        !cxpr_cuda_append_bytes(
            &replacement,
            body_start,
            (size_t)(func_close - body_start))) {
        free(call);
        free(without_init.data);
        free(replacement.data);
        return NULL;
    }
    if (!cxpr_cuda_append(&replacement, "    }\n")) {
        free(call);
        free(without_init.data);
        free(replacement.data);
        return NULL;
    }

    out = cxpr_cuda_replace_all(without_init.data, call, replacement.data);
    free(call);
    free(without_init.data);
    free(replacement.data);
    return out;

unchanged:
    if (!cxpr_cuda_append(&without_init, source)) return NULL;
    return without_init.data;
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

static int cxpr_cuda_append_math_binding_unconditional(
    cxpr_cuda_buf* out,
    const char* extern_decl,
    const char* macro_decl) {
    return cxpr_cuda_append(out, extern_decl) &&
           cxpr_cuda_append(out, macro_decl);
}

static const char* cxpr_cuda_model_runtime_source =
    "#ifndef CXPR_MODEL_RUNTIME_H\n"
    "#define CXPR_MODEL_RUNTIME_H\n"
    "typedef unsigned long size_t;\n"
    "#ifndef NAN\n"
    "#define NAN (0.0 / 0.0)\n"
    "#endif\n"
    "#define cxpr_model_runtime_isnan(x) ((x) != (x))\n"
    "#ifndef CXPR_MODEL_RUNTIME_LINKAGE\n"
    "#define CXPR_MODEL_RUNTIME_LINKAGE static __device__ inline\n"
    "#endif\n"
    "CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_eval_c(const double* values, size_t count, int period, int op) {\n"
    "    double sum = 0.0;\n"
    "    double sumsq = 0.0;\n"
    "    double extreme = 0.0;\n"
    "    size_t valid_count = 0u;\n"
    "    size_t limit = period < 1 ? 1u : (size_t)period;\n"
    "    if (!values || count == 0u) return 0.0;\n"
    "    if (limit > count) limit = count;\n"
    "    for (size_t i = 0u; i < limit; ++i) {\n"
    "        double x = values[i];\n"
    "        if (cxpr_model_runtime_isnan(x)) continue;\n"
    "        if (valid_count == 0u) extreme = x;\n"
    "        if (op == 2 && x > extreme) extreme = x;\n"
    "        if (op == 3 && x < extreme) extreme = x;\n"
    "        sum += x;\n"
    "        sumsq += x * x;\n"
    "        valid_count++;\n"
    "    }\n"
    "    if (valid_count == 0u) return 0.0;\n"
    "    if (op == 2 || op == 3) return extreme;\n"
    "    if (op == 1) return sum / (double)valid_count;\n"
    "    if (op == 4) {\n"
    "        double mean = sum / (double)valid_count;\n"
    "        double variance = (sumsq / (double)valid_count) - mean * mean;\n"
    "        return sqrt(variance > 0.0 ? variance : 0.0);\n"
    "    }\n"
    "    return sum;\n"
    "}\n"
    "CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_roc_c(const double* values, size_t count, int period) {\n"
    "    size_t index = period < 1 ? 1u : (size_t)period;\n"
    "    double now;\n"
    "    double prev;\n"
    "    if (!values || count == 0u) return 0.0;\n"
    "    if (index >= count) index = count - 1u;\n"
    "    now = values[0];\n"
    "    prev = values[index];\n"
    "    if (cxpr_model_runtime_isnan(now)) return NAN;\n"
    "    if (cxpr_model_runtime_isnan(prev) || fabs(prev) <= 1e-12) return 0.0;\n"
    "    return ((now - prev) / prev) * 100.0;\n"
    "}\n"
    "CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_mean_roc_c(const double* values, size_t count, int roc_period, int mean_period) {\n"
    "    size_t rp = roc_period < 1 ? 1u : (size_t)roc_period;\n"
    "    size_t mp = mean_period < 1 ? 1u : (size_t)mean_period;\n"
    "    double sum = 0.0;\n"
    "    size_t valid_count = 0u;\n"
    "    if (!values || count == 0u) return 0.0;\n"
    "    if (mp > count) mp = count;\n"
    "    for (size_t i = 0u; i < mp; ++i) {\n"
    "        double now = values[i];\n"
    "        double prev = (i + rp < count) ? values[i + rp] : NAN;\n"
    "        double roc;\n"
    "        if (cxpr_model_runtime_isnan(now)) continue;\n"
    "        roc = (cxpr_model_runtime_isnan(prev) || fabs(prev) <= 1e-12) ? 0.0 : ((now - prev) / prev) * 100.0;\n"
    "        sum += roc;\n"
    "        valid_count++;\n"
    "    }\n"
    "    return valid_count == 0u ? 0.0 : sum / (double)valid_count;\n"
    "}\n"
    "CXPR_MODEL_RUNTIME_LINKAGE double cxpr_model_window_midpoint_c(const double* highs, const double* lows, size_t count, int period) {\n"
    "    size_t limit = period < 1 ? 1u : (size_t)period;\n"
    "    double highest = 0.0;\n"
    "    double lowest = 0.0;\n"
    "    size_t valid_count = 0u;\n"
    "    if (!highs || !lows || count == 0u) return 0.0;\n"
    "    if (limit > count) limit = count;\n"
    "    for (size_t i = 0u; i < limit; ++i) {\n"
    "        double hi = highs[i];\n"
    "        double lo = lows[i];\n"
    "        if (cxpr_model_runtime_isnan(hi) || cxpr_model_runtime_isnan(lo)) continue;\n"
    "        if (valid_count == 0u) { highest = hi; lowest = lo; }\n"
    "        if (hi > highest) highest = hi;\n"
    "        if (lo < lowest) lowest = lo;\n"
    "        valid_count++;\n"
    "    }\n"
    "    return valid_count == 0u ? 0.0 : (highest + lowest) * 0.5;\n"
    "}\n"
    "#endif\n\n";

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
    char* runtime_patched;
    char* init_patched;
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
        "static __device__ double cxpr_fn_");
    if (!patched) {
        free(code);
        return NULL;
    }
    runtime_patched = cxpr_cuda_replace_all(
        patched,
        "#include <cxpr/model/runtime.h>\n\n",
        cxpr_cuda_model_runtime_source);
    free(patched);
    if (!runtime_patched) {
        free(code);
        return NULL;
    }
    init_patched = cxpr_cuda_replace_all(
        runtime_patched,
        "#include <stdint.h>\n\n",
        "typedef unsigned char uint8_t;\n\n");
    free(runtime_patched);
    if (!init_patched) {
        free(code);
        return NULL;
    }
    {
        char* inlined = cxpr_cuda_inline_init_state(init_patched, function_name);
        free(init_patched);
        if (!inlined) {
            free(code);
            return NULL;
        }
        cuda_patched = cxpr_cuda_replace_all(inlined, " restrict ", " __restrict__ ");
        free(inlined);
    }
    if (!cuda_patched) {
        free(code);
        return NULL;
    }

    if (!cxpr_cuda_append_math_binding_unconditional(
            &out,
            "extern \"C\" __device__ double __nv_fabs(double);\n",
            "#define fabs(x) __nv_fabs((double)(x))\n") ||
        !cxpr_cuda_append_math_binding_unconditional(
            &out,
            "extern \"C\" __device__ double __nv_sqrt(double);\n",
            "#define sqrt(x) __nv_sqrt((double)(x))\n") ||
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
            "",
            "#define round(x) (((double)(x)) >= 0.0 ? (double)((long long)(((double)(x)) + 0.5)) : (double)((long long)(((double)(x)) - 0.5)))\n") ||
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
    const cxpr_model_plugin_event* event,
    const cxpr_cuda_plugin_options* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err) {
    cxpr_model_plugin_artifact_event artifact = {
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
    const cxpr_model_plugin_event* event,
    const void* options,
    const cxpr_model_plugin_host* host,
    cxpr_error* err) {
    return cxpr_cuda_plugin_emit_source(
        event,
        (const cxpr_cuda_plugin_options*)options,
        host,
        err);
}

const cxpr_model_plugin_backend* cxpr_cuda_plugin_backend(void) {
    static const cxpr_model_plugin_backend backend = {
        "cxpr.cuda.source",
        cxpr_cuda_plugin_generate_bridge
    };
    return &backend;
}
