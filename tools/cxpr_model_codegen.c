#include <cxpr/cxpr.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* resolve_import_path_for_model(const char* dir, const char* use_name);

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    long size;
    char* data;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    data = (char*)malloc((size_t)size + 1u);
    if (!data) {
        fclose(f);
        return NULL;
    }
    if (fread(data, 1u, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = '\0';
    fclose(f);
    return data;
}

static char* xstrndup(const char* s, size_t n) {
    char* out = (char*)malloc(n + 1u);
    if (!out) return NULL;
    memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char* xstrdup(const char* s) {
    return s ? xstrndup(s, strlen(s)) : NULL;
}

static char* trim_in_place(char* s) {
    char* end;
    while (*s && isspace((unsigned char)*s)) s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static int keyword_line(const char* line, const char* keyword, const char** rest) {
    size_t n = strlen(keyword);
    if (strncmp(line, keyword, n) != 0) return 0;
    if (line[n] != '\0' && !isspace((unsigned char)line[n])) return 0;
    if (rest) {
        const char* r = line + n;
        while (*r && isspace((unsigned char)*r)) r++;
        *rest = r;
    }
    return 1;
}

static int brace_delta(const char* s) {
    int delta = 0;
    for (; *s; ++s) {
        if (*s == '{') delta++;
        else if (*s == '}') delta--;
    }
    return delta;
}

static int append_bytes(char** out, size_t* len, size_t* cap, const char* text, size_t n) {
    char* next;
    if (*len + n + 1u > *cap) {
        size_t new_cap = *cap ? *cap : 256u;
        while (*len + n + 1u > new_cap) new_cap *= 2u;
        next = (char*)realloc(*out, new_cap);
        if (!next) return 0;
        *out = next;
        *cap = new_cap;
    }
    memcpy(*out + *len, text, n);
    *len += n;
    (*out)[*len] = '\0';
    return 1;
}

static int append_cstr(char** out, size_t* len, size_t* cap, const char* text) {
    return append_bytes(out, len, cap, text, strlen(text));
}

static char* path_dirname(const char* path) {
    const char* slash = strrchr(path, '/');
    if (!slash) return xstrdup(".");
    if (slash == path) return xstrdup("/");
    return xstrndup(path, (size_t)(slash - path));
}

static char* join_import_path(const char* dir, const char* name) {
    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int need_slash = dir_len > 0u && dir[dir_len - 1u] != '/';
    int has_suffix = name_len > 5u && strcmp(name + name_len - 5u, ".cxpr") == 0;
    char* out = (char*)malloc(dir_len + (need_slash ? 1u : 0u) + name_len +
                              (has_suffix ? 1u : 6u));
    if (!out) return NULL;
    sprintf(out, "%s%s%s%s", dir, need_slash ? "/" : "", name, has_suffix ? "" : ".cxpr");
    return out;
}

static int string_list_contains(char* const* values, size_t count, const char* value) {
    for (size_t i = 0u; i < count; ++i) {
        if (strcmp(values[i], value) == 0) return 1;
    }
    return 0;
}

static int string_list_add(char*** values, size_t* count, const char* value) {
    char** next;
    if (string_list_contains(*values, *count, value)) return 1;
    next = (char**)realloc(*values, (*count + 1u) * sizeof(char*));
    if (!next) return 0;
    *values = next;
    (*values)[*count] = xstrdup(value);
    if (!(*values)[*count]) return 0;
    (*count)++;
    return 1;
}

static void string_list_free(char** values, size_t count) {
    if (!values) return;
    for (size_t i = 0u; i < count; ++i) free(values[i]);
    free(values);
}

static char* extract_function_sources(const char* source) {
    const char* p = source;
    char* current = NULL;
    size_t current_len = 0u;
    size_t current_cap = 0u;
    char* out = NULL;
    size_t out_len = 0u;
    size_t out_cap = 0u;
    int current_is_fn = 0;
    int current_brace_depth = 0;
    int skipping_meta = 0;
    int meta_brace_depth = 0;

    while (*p) {
        const char* line_start = p;
        const char* line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line_start) : strlen(line_start);
        char* line = xstrndup(line_start, line_len);
        char* trimmed;
        int continuation;
        const char* rest = NULL;

        if (!line) goto oom;
        if (line[0] == '\r') memmove(line, line + 1, strlen(line));
        trimmed = trim_in_place(line);
        if (*trimmed == '\0' || *trimmed == '#' ||
            (trimmed[0] == '/' && trimmed[1] == '/')) {
            free(line);
            p = line_end ? line_end + 1 : line_start + line_len;
            continue;
        }

        if (skipping_meta) {
            meta_brace_depth += brace_delta(trimmed);
            if (meta_brace_depth <= 0) {
                skipping_meta = 0;
                meta_brace_depth = 0;
            }
            free(line);
            p = line_end ? line_end + 1 : line_start + line_len;
            continue;
        }

        continuation = current && (current_brace_depth > 0 ||
                                   (line_start[0] && isspace((unsigned char)line_start[0])));
        if (!continuation && current) {
            if (current_is_fn) {
                if (!append_cstr(&out, &out_len, &out_cap, current) ||
                    !append_cstr(&out, &out_len, &out_cap, "\n")) {
                    free(line);
                    goto oom;
                }
            }
            free(current);
            current = NULL;
            current_len = 0u;
            current_cap = 0u;
            current_is_fn = 0;
            current_brace_depth = 0;
        }

        if (!current && keyword_line(trimmed, "meta", &rest)) {
            meta_brace_depth = brace_delta(trimmed);
            if (meta_brace_depth > 0) skipping_meta = 1;
            free(line);
            p = line_end ? line_end + 1 : line_start + line_len;
            continue;
        }

        if (!current) current_is_fn = keyword_line(trimmed, "fn", NULL);
        if (!append_cstr(&current, &current_len, &current_cap, trimmed) ||
            !append_cstr(&current, &current_len, &current_cap, " ")) {
            free(line);
            goto oom;
        }
        current_brace_depth += brace_delta(trimmed);

        free(line);
        p = line_end ? line_end + 1 : line_start + line_len;
    }

    if (current) {
        if (current_is_fn) {
            if (!append_cstr(&out, &out_len, &out_cap, current) ||
                !append_cstr(&out, &out_len, &out_cap, "\n")) {
                goto oom;
            }
        }
        free(current);
    }
    if (!out && !append_cstr(&out, &out_len, &out_cap, "")) return NULL;
    return out;

oom:
    free(current);
    free(out);
    return NULL;
}

static int collect_import_functions(const char* model_path,
                                    const char* source,
                                    char*** visited,
                                    size_t* visited_count,
                                    char** out,
                                    size_t* out_len,
                                    size_t* out_cap) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    char* dir = NULL;
    int ok = 0;

    if (!model) {
        fprintf(stderr, "cxpr_model_codegen: parse failed while resolving imports for %s: %s\n",
                model_path, err.message ? err.message : "(null)");
        return 0;
    }

    dir = path_dirname(model_path);
    if (!dir) goto cleanup;

    for (size_t i = 0u; i < cxpr_model_use_count(model); ++i) {
        const char* use_name = cxpr_model_use(model, i);
        char* import_path;
        char* import_source;
        char* import_functions;

        import_path = resolve_import_path_for_model(dir, use_name);
        if (!import_path) goto cleanup;
        if (string_list_contains(*visited, *visited_count, import_path)) {
            free(import_path);
            continue;
        }
        if (!string_list_add(visited, visited_count, import_path)) {
            free(import_path);
            goto cleanup;
        }
        import_source = read_file(import_path);
        if (!import_source) {
            fprintf(stderr, "cxpr_model_codegen: failed to read import %s for use %s\n",
                    import_path, use_name);
            free(import_path);
            goto cleanup;
        }

        if (!collect_import_functions(import_path, import_source,
                                      visited, visited_count, out, out_len, out_cap)) {
            free(import_source);
            free(import_path);
            goto cleanup;
        }

        import_functions = extract_function_sources(import_source);
        if (!import_functions) {
            fprintf(stderr, "cxpr_model_codegen: failed to extract functions from import %s\n",
                    import_path);
            free(import_source);
            free(import_path);
            goto cleanup;
        }
        if (!append_cstr(out, out_len, out_cap, import_functions) ||
            !append_cstr(out, out_len, out_cap, "\n")) {
            free(import_functions);
            free(import_source);
            free(import_path);
            goto cleanup;
        }
        free(import_functions);
        free(import_source);
        free(import_path);
    }

    ok = 1;

cleanup:
    free(dir);
    cxpr_model_free(model);
    return ok;
}

static char* build_source_with_imports(const char* model_path, const char* source) {
    char** visited = NULL;
    size_t visited_count = 0u;
    char* combined = NULL;
    size_t combined_len = 0u;
    size_t combined_cap = 0u;

    if (!collect_import_functions(model_path, source,
                                  &visited, &visited_count,
                                  &combined, &combined_len, &combined_cap)) {
        string_list_free(visited, visited_count);
        free(combined);
        return NULL;
    }
    if (!append_cstr(&combined, &combined_len, &combined_cap, source)) {
        string_list_free(visited, visited_count);
        free(combined);
        return NULL;
    }
    string_list_free(visited, visited_count);
    return combined;
}

static void usage(const char* argv0) {
    fprintf(stderr,
            "usage: %s --model <model.cxpr> [--output <out.c>] "
            "[--meta-output <out.json>] [--graph-output <out.json>] "
            "[--function <name>] [--qualifiers <text>] "
            "[--outputs <name[,name...]>] [--specialize-defaults]\n",
            argv0);
}

static void output_selection_free(size_t* indices) {
    free(indices);
}

static int output_selection_parse(const cxpr_model_program* program,
                                  const char* csv,
                                  size_t** out_indices,
                                  size_t* out_count) {
    char* copy;
    char* p;
    size_t* indices = NULL;
    size_t count = 0u;

    if (out_indices) *out_indices = NULL;
    if (out_count) *out_count = 0u;
    if (!csv || !csv[0]) return 1;
    copy = xstrdup(csv);
    if (!copy) return 0;
    p = copy;
    while (p && *p) {
        char* comma = strchr(p, ',');
        char* name;
        size_t found = (size_t)-1;
        size_t* next;
        if (comma) *comma = '\0';
        name = trim_in_place(p);
        if (name[0] == '\0') {
            free(copy);
            free(indices);
            return 0;
        }
        for (size_t i = 0u; i < cxpr_model_program_output_count(program); ++i) {
            const char* candidate = cxpr_model_program_output_name(program, i);
            if (candidate && strcmp(candidate, name) == 0) {
                found = i;
                break;
            }
        }
        if (found == (size_t)-1) {
            fprintf(stderr, "cxpr_model_codegen: unknown output '%s'\n", name);
            free(copy);
            free(indices);
            return 0;
        }
        next = (size_t*)realloc(indices, (count + 1u) * sizeof(size_t));
        if (!next) {
            free(copy);
            free(indices);
            return 0;
        }
        indices = next;
        indices[count++] = found;
        p = comma ? comma + 1 : NULL;
    }
    free(copy);
    if (out_indices) *out_indices = indices;
    else free(indices);
    if (out_count) *out_count = count;
    return 1;
}

typedef struct artifact_file_sink {
    const char* path;
    FILE* file;
} artifact_file_sink;

typedef struct compiled_model_import {
    cxpr_model_import api;
    char* source;
    cxpr_model* model;
    cxpr_model_program* program;
} compiled_model_import;

static char* resolve_import_path_for_model(const char* dir, const char* use_name) {
    char* path = join_import_path(dir, use_name);
    FILE* f;
    if (!path) return NULL;
    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return path;
    }
    free(path);
    if (strncmp(use_name, "indicators/", strlen("indicators/")) == 0) {
        size_t len = strlen("libs/dyn/cxpr/") + strlen(use_name) + strlen(".cxpr") + 1u;
        path = (char*)malloc(len);
        if (!path) return NULL;
        snprintf(path, len, "libs/dyn/cxpr/%s.cxpr", use_name);
        f = fopen(path, "rb");
        if (f) {
            fclose(f);
            return path;
        }
        free(path);
    }
    return join_import_path(dir, use_name);
}

static void compiled_imports_free(compiled_model_import* imports, size_t count) {
    if (!imports) return;
    for (size_t i = 0u; i < count; ++i) {
        cxpr_model_program_free(imports[i].program);
        cxpr_model_free(imports[i].model);
        free(imports[i].source);
    }
    free(imports);
}

static int build_model_imports(const char* model_path,
                               const char* source,
                               compiled_model_import** out_imports,
                               size_t* out_count) {
    cxpr_error err = {0};
    cxpr_model* model = cxpr_parse_model(source, &err);
    char* dir = NULL;
    compiled_model_import* imports = NULL;
    size_t count = 0u;
    int ok = 0;

    if (out_imports) *out_imports = NULL;
    if (out_count) *out_count = 0u;
    if (!model) return 0;
    dir = path_dirname(model_path);
    if (!dir) goto cleanup;

    for (size_t i = 0u; i < cxpr_model_use_count(model); ++i) {
        const char* use_name = cxpr_model_use(model, i);
        char* import_path = resolve_import_path_for_model(dir, use_name);
        char* import_source;
        char* import_combined;
        cxpr_model* import_model;
        cxpr_model_program* import_program;
        compiled_model_import* grown;
        const char* model_name;
        if (!import_path) goto cleanup;
        import_source = read_file(import_path);
        if (!import_source) {
            free(import_path);
            goto cleanup;
        }
        import_combined = build_source_with_imports(import_path, import_source);
        free(import_source);
        if (!import_combined) {
            free(import_path);
            goto cleanup;
        }
        import_model = cxpr_parse_model(import_combined, &err);
        if (!import_model) {
            free(import_combined);
            free(import_path);
            goto cleanup;
        }
        if (cxpr_model_output_count(import_model) == 0u) {
            cxpr_model_free(import_model);
            free(import_combined);
            free(import_path);
            continue;
        }
        import_program = cxpr_compile_model(import_model, NULL, &err);
        if (!import_program) {
            cxpr_model_free(import_model);
            free(import_combined);
            free(import_path);
            goto cleanup;
        }
        grown = (compiled_model_import*)realloc(imports, (count + 1u) * sizeof(*imports));
        if (!grown) {
            cxpr_model_program_free(import_program);
            cxpr_model_free(import_model);
            free(import_combined);
            free(import_path);
            goto cleanup;
        }
        imports = grown;
        memset(&imports[count], 0, sizeof(imports[count]));
        model_name = cxpr_model_name(import_model);
        imports[count].api.name = model_name ? model_name : use_name;
        imports[count].api.program = import_program;
        imports[count].source = import_combined;
        imports[count].model = import_model;
        imports[count].program = import_program;
        count++;
        free(import_path);
    }
    ok = 1;

cleanup:
    free(dir);
    cxpr_model_free(model);
    if (!ok) {
        compiled_imports_free(imports, count);
        return 0;
    }
    if (out_imports) *out_imports = imports;
    if (out_count) *out_count = count;
    return 1;
}

static int artifact_file_begin(void* user, const cxpr_plugin_artifact_event* artifact, cxpr_error* err) {
    artifact_file_sink* sink = (artifact_file_sink*)user;
    (void)artifact;
    (void)err;
    if (!sink || !sink->path) return 0;
    sink->file = fopen(sink->path, "wb");
    return sink->file != NULL;
}

static int artifact_file_write(void* user, const void* data, size_t size, cxpr_error* err) {
    artifact_file_sink* sink = (artifact_file_sink*)user;
    (void)err;
    return sink && sink->file && fwrite(data, 1u, size, sink->file) == size;
}

static int artifact_file_end(void* user, cxpr_error* err) {
    artifact_file_sink* sink = (artifact_file_sink*)user;
    int ok;
    (void)err;
    if (!sink || !sink->file) return 0;
    ok = fclose(sink->file) == 0;
    sink->file = NULL;
    return ok;
}

static int emit_model_c(const char* model_path,
                        const char* output_path,
                        const char* meta_output_path,
                        const char* graph_output_path,
                        const char* function_name,
                        const char* qualifiers,
                        const char* selected_outputs_csv,
                        int specialize_defaults) {
    cxpr_error err = {0};
    char* source = NULL;
    char* combined_source = NULL;
    cxpr_model* model = NULL;
    cxpr_model_program* program = NULL;
    compiled_model_import* compiled_imports = NULL;
    cxpr_model_import* import_api = NULL;
    size_t import_count = 0u;
    cxpr_context* ctx = NULL;
    double* param_values = NULL;
    size_t param_count = 0u;
    size_t* output_indices = NULL;
    size_t output_count = 0u;
    artifact_file_sink meta_sink = {0};
    artifact_file_sink graph_sink = {0};
    int rc = 1;

    source = read_file(model_path);
    if (!source) {
        fprintf(stderr, "cxpr_model_codegen: failed to read %s\n", model_path);
        goto cleanup;
    }
    combined_source = build_source_with_imports(model_path, source);
    if (!combined_source) {
        fprintf(stderr, "cxpr_model_codegen: failed to resolve model imports\n");
        goto cleanup;
    }
    model = cxpr_parse_model(combined_source, &err);
    if (!model) {
        fprintf(stderr, "cxpr_model_codegen: parse failed: %s\n", err.message);
        goto cleanup;
    }
    if (!build_model_imports(model_path, source, &compiled_imports, &import_count)) {
        fprintf(stderr, "cxpr_model_codegen: failed to compile model imports\n");
        goto cleanup;
    }
    if (import_count > 0u) {
        import_api = (cxpr_model_import*)calloc(import_count, sizeof(*import_api));
        if (!import_api) goto cleanup;
        for (size_t i = 0u; i < import_count; ++i) import_api[i] = compiled_imports[i].api;
    }
    program = cxpr_compile_model_with_imports(model, NULL, import_api, import_count, &err);
    if (!program) {
        fprintf(stderr, "cxpr_model_codegen: compile failed: %s\n", err.message);
        goto cleanup;
    }

    param_count = cxpr_model_program_c_param_count(program);
    if (specialize_defaults && param_count > 0u) {
        ctx = cxpr_context_new();
        param_values = (double*)calloc(param_count, sizeof(double));
        if (!ctx || !param_values) {
            fprintf(stderr, "cxpr_model_codegen: out of memory preparing params\n");
            goto cleanup;
        }
        if (!cxpr_model_program_seed_defaults(program, ctx, NULL, &err)) {
            fprintf(stderr, "cxpr_model_codegen: default param eval failed: %s\n", err.message);
            goto cleanup;
        }
        for (size_t i = 0u; i < param_count; ++i) {
            bool found = false;
            const char* name = cxpr_model_program_c_param_name(program, i);
            param_values[i] = cxpr_context_get_param(ctx, name, &found);
            if (!found) {
                fprintf(stderr, "cxpr_model_codegen: default param missing: %s\n",
                        name ? name : "(null)");
                goto cleanup;
            }
        }
    }
    if (selected_outputs_csv &&
        !output_selection_parse(program, selected_outputs_csv, &output_indices, &output_count)) {
        goto cleanup;
    }

    if (output_path) {
        cxpr_plugin_host host;
        cxpr_plugin_model_event event = {0};
        cxpr_c_plugin_options c_options = {0};

        artifact_file_sink c_sink = {0};
        c_sink.path = output_path;
        host.user = &c_sink;
        host.begin_artifact = artifact_file_begin;
        host.write_artifact = artifact_file_write;
        host.end_artifact = artifact_file_end;
        event.model_path = model_path;
        event.model = model;
        event.program = program;
        c_options.function_name = function_name;
        c_options.qualifiers = qualifiers;
        c_options.param_values = param_values;
        c_options.param_count = param_values ? param_count : 0u;
        c_options.output_indices = output_indices;
        c_options.output_count = output_count;
        c_options.include_headers = 1;
        if (!cxpr_plugin_run_model_backend(
                &event, cxpr_c_plugin_backend(), &c_options, &host, &err)) {
            fprintf(stderr, "cxpr_model_codegen: C emit failed: %s\n",
                    err.message ? err.message : "(null)");
            if (c_sink.file) {
                fclose(c_sink.file);
                c_sink.file = NULL;
            }
            goto cleanup;
        }
    }

    if (meta_output_path) {
        cxpr_plugin_host host;
        cxpr_plugin_model_event event = {0};

        meta_sink.path = meta_output_path;
        host.user = &meta_sink;
        host.begin_artifact = artifact_file_begin;
        host.write_artifact = artifact_file_write;
        host.end_artifact = artifact_file_end;
        event.model_path = model_path;
        event.model = model;
        event.program = program;
        if (!cxpr_meta_plugin_emit_manifest(&event, NULL, &host, &err)) {
            fprintf(stderr, "cxpr_model_codegen: meta emit failed: %s\n",
                    err.message ? err.message : "(null)");
            goto cleanup;
        }
    }

    if (graph_output_path) {
        cxpr_plugin_host host;
        cxpr_plugin_model_event event = {0};

        graph_sink.path = graph_output_path;
        host.user = &graph_sink;
        host.begin_artifact = artifact_file_begin;
        host.write_artifact = artifact_file_write;
        host.end_artifact = artifact_file_end;
        event.model_path = model_path;
        event.model = model;
        event.program = program;
        if (!cxpr_graph_plugin_emit_graph(&event, NULL, &host, &err)) {
            fprintf(stderr, "cxpr_model_codegen: graph emit failed: %s\n",
                    err.message ? err.message : "(null)");
            goto cleanup;
        }
    }

    rc = 0;

cleanup:
    if (meta_sink.file) fclose(meta_sink.file);
    if (graph_sink.file) fclose(graph_sink.file);
    output_selection_free(output_indices);
    free(param_values);
    cxpr_context_free(ctx);
    cxpr_model_program_free(program);
    free(import_api);
    compiled_imports_free(compiled_imports, import_count);
    cxpr_model_free(model);
    free(combined_source);
    free(source);
    return rc;
}

int main(int argc, char** argv) {
    const char* model_path = NULL;
    const char* output_path = NULL;
    const char* meta_output_path = NULL;
    const char* graph_output_path = NULL;
    const char* function_name = "cxpr_model_tick";
    const char* qualifiers = NULL;
    const char* selected_outputs_csv = NULL;
    int specialize_defaults = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--model") == 0 && i + 1 < argc) {
            model_path = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "--meta-output") == 0 && i + 1 < argc) {
            meta_output_path = argv[++i];
        } else if (strcmp(argv[i], "--graph-output") == 0 && i + 1 < argc) {
            graph_output_path = argv[++i];
        } else if (strcmp(argv[i], "--function") == 0 && i + 1 < argc) {
            function_name = argv[++i];
        } else if (strcmp(argv[i], "--qualifiers") == 0 && i + 1 < argc) {
            qualifiers = argv[++i];
        } else if (strcmp(argv[i], "--outputs") == 0 && i + 1 < argc) {
            selected_outputs_csv = argv[++i];
        } else if (strcmp(argv[i], "--specialize-defaults") == 0) {
            specialize_defaults = 1;
        } else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!model_path || (!output_path && !meta_output_path && !graph_output_path) ||
        !function_name || function_name[0] == '\0') {
        usage(argv[0]);
        return 2;
    }
    return emit_model_c(model_path,
                        output_path,
                        meta_output_path,
                        graph_output_path,
                        function_name,
                        qualifiers,
                        selected_outputs_csv,
                        specialize_defaults);
}
