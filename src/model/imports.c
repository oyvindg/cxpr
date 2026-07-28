#include <cxpr/model/imports.h>

#include <stdlib.h>
#include <string.h>

typedef struct cxpr_model_import_node {
    char* id;
    char* namespace_name;
    char* source;
    cxpr_model* model;
    cxpr_model_program* program;
    bool function_only;
} cxpr_model_import_node;

struct cxpr_model_import_bundle {
    cxpr_model_import_node* nodes;
    size_t node_count;
    cxpr_model_import* root_imports;
    size_t root_import_count;
};

typedef struct cxpr_model_import_builder {
    cxpr_model_import_bundle* bundle;
    cxpr_model_import_load_fn load;
    void* userdata;
    const char** stack;
    size_t stack_count;
} cxpr_model_import_builder;

static char* import_strdup(const char* value) {
    size_t size;
    char* copy;
    if (!value) return NULL;
    size = strlen(value) + 1u;
    copy = (char*)malloc(size);
    if (copy) memcpy(copy, value, size);
    return copy;
}

static void import_error(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->position = 0u;
    err->line = 0u;
    err->column = 0u;
}

static cxpr_model_import_node* import_find(
    const cxpr_model_import_bundle* graph,
    const char* id) {
    size_t i;
    if (!graph || !id) return NULL;
    for (i = 0u; i < graph->node_count; ++i) {
        if (graph->nodes[i].id && strcmp(graph->nodes[i].id, id) == 0) {
            return &graph->nodes[i];
        }
    }
    return NULL;
}

static bool import_stack_contains(
    const cxpr_model_import_builder* builder,
    const char* id) {
    size_t i;
    for (i = 0u; i < builder->stack_count; ++i) {
        if (strcmp(builder->stack[i], id) == 0) return true;
    }
    return false;
}

static char* import_make_compilable_source(
    const char* source,
    const cxpr_model* model) {
    static const char header[] = "model __cxpr_import\n";
    static const char output[] = "\nout __cxpr_import_dummy = 0\n";
    size_t header_size = cxpr_model_name(model) ? 0u : sizeof(header) - 1u;
    size_t source_size = strlen(source);
    size_t output_size =
        cxpr_model_output_count(model) == 0u ? sizeof(output) - 1u : 0u;
    char* result = (char*)malloc(header_size + source_size + output_size + 1u);
    if (!result) return NULL;
    if (header_size) memcpy(result, header, header_size);
    memcpy(result + header_size, source, source_size);
    if (output_size) {
        memcpy(result + header_size + source_size, output, output_size);
    }
    result[header_size + source_size + output_size] = '\0';
    return result;
}

static char* import_prepend_source(const char* prefix, const char* source) {
    size_t prefix_size = prefix ? strlen(prefix) : 0u;
    size_t source_size = source ? strlen(source) : 0u;
    char* result = (char*)malloc(prefix_size + source_size + 2u);
    if (!result) return NULL;
    if (prefix_size) memcpy(result, prefix, prefix_size);
    result[prefix_size] = '\n';
    if (source_size) memcpy(result + prefix_size + 1u, source, source_size);
    result[prefix_size + source_size + 1u] = '\0';
    return result;
}

static bool import_compile(
    cxpr_model_import_builder* builder,
    const char* importer_id,
    const char* use_path,
    cxpr_model_import_node** out_node,
    cxpr_error* err) {
    char* id = NULL;
    char* source = NULL;
    char* combined = NULL;
    char* compilable = NULL;
    cxpr_model* model = NULL;
    cxpr_model_program* program = NULL;
    cxpr_model_import* direct = NULL;
    size_t direct_count = 0u;
    bool function_only;
    cxpr_model_import_node* node;
    const char** grown_stack;
    size_t i;

    *out_node = NULL;
    if (!builder->load(importer_id, use_path, builder->userdata,
                       &id, &source, err)) {
        free(id);
        free(source);
        if (err && err->code == CXPR_OK) {
            import_error(err, CXPR_ERR_SYNTAX, "Failed to load model import");
        }
        return false;
    }
    if (!id && !source) return true;
    if (!id || !source) {
        free(id);
        free(source);
        import_error(err, CXPR_ERR_SYNTAX,
                     "Model import loader returned an incomplete result");
        return false;
    }
    node = import_find(builder->bundle, id);
    if (node) {
        free(id);
        free(source);
        *out_node = node;
        return true;
    }
    if (import_stack_contains(builder, id)) {
        free(id);
        free(source);
        import_error(err, CXPR_ERR_CIRCULAR_DEPENDENCY,
                     "Circular model import dependency");
        return false;
    }
    grown_stack = (const char**)realloc(
        builder->stack, (builder->stack_count + 1u) * sizeof(*grown_stack));
    if (!grown_stack) goto oom;
    builder->stack = grown_stack;
    builder->stack[builder->stack_count++] = id;

    model = cxpr_parse_model_source(source, err);
    if (!model) goto fail;
    function_only = cxpr_model_output_count(model) == 0u;
    for (i = 0u; i < cxpr_model_use_count(model); ++i) {
        cxpr_model_import_node* child;
        cxpr_model_import* grown;
        const char* child_use = cxpr_model_use(model, i);
        if (!import_compile(builder, id, child_use, &child, err)) goto fail;
        if (!child) continue;
        grown = (cxpr_model_import*)realloc(
            direct, (direct_count + 1u) * sizeof(*direct));
        if (!grown) goto oom;
        direct = grown;
        direct[direct_count].name =
            cxpr_model_use_alias(model, i)
                ? cxpr_model_use_alias(model, i)
                : child_use;
        direct[direct_count].program = child->program;
        direct_count++;
        if (child->function_only) {
            char* prefixed = import_prepend_source(
                child->source, combined ? combined : source);
            if (!prefixed) goto oom;
            if (combined) free(combined);
            combined = prefixed;
        }
    }
    if (combined) {
        cxpr_model_free(model);
        model = cxpr_parse_model_source(combined, err);
        if (!model) goto fail;
    }
    compilable = import_make_compilable_source(combined ? combined : source, model);
    if (!compilable) goto oom;
    if (strcmp(compilable, source) != 0) {
        cxpr_model_free(model);
        model = cxpr_parse_model_source(compilable, err);
        if (!model) goto fail;
    }
    program = cxpr_compile_model_with_imports(
        model, NULL, direct, direct_count, err);
    if (!program) goto fail;
    {
        cxpr_model_import_node* grown;
        const char* model_name;
        const char* namespace_name;
        size_t existing;
        model_name = cxpr_model_name(model);
        namespace_name =
            model_name && strcmp(model_name, "__cxpr_import") != 0
                ? model_name
                : use_path;
        for (existing = 0u; existing < builder->bundle->node_count; ++existing) {
            const cxpr_model_import_node* other =
                &builder->bundle->nodes[existing];
            if (other->namespace_name &&
                strcmp(other->namespace_name, namespace_name) == 0 &&
                strcmp(other->id, id) != 0) {
                import_error(err, CXPR_ERR_SYNTAX,
                             "Duplicate model import namespace");
                goto fail;
            }
        }
        grown = (cxpr_model_import_node*)realloc(
            builder->bundle->nodes,
            (builder->bundle->node_count + 1u) * sizeof(*grown));
        if (!grown) goto oom;
        builder->bundle->nodes = grown;
        node = &grown[builder->bundle->node_count++];
        memset(node, 0, sizeof(*node));
        node->id = id;
        node->namespace_name = import_strdup(namespace_name);
        node->source = combined ? combined : source;
        node->model = model;
        node->program = program;
        node->function_only = function_only;
        if (!node->namespace_name) {
            builder->bundle->node_count--;
            memset(node, 0, sizeof(*node));
            goto oom;
        }
    }
    builder->stack_count--;
    free(direct);
    if (combined) free(source);
    free(compilable);
    *out_node = node;
    return true;

oom:
    import_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
fail:
    if (builder->stack_count > 0u &&
        builder->stack[builder->stack_count - 1u] == id) {
        builder->stack_count--;
    }
    free(direct);
    cxpr_model_program_free(program);
    cxpr_model_free(model);
    free(compilable);
    free(combined);
    free(source);
    free(id);
    return false;
}

cxpr_model_import_bundle* cxpr_model_import_bundle_build(
    const char* root_id,
    const cxpr_model* root_model,
    cxpr_model_import_load_fn load,
    void* userdata,
    cxpr_error* err) {
    cxpr_model_import_bundle* graph;
    cxpr_model_import_builder builder = {0};
    size_t i;
    if (err) *err = (cxpr_error){0};
    if (!root_id || !root_model || !load) {
        import_error(err, CXPR_ERR_SYNTAX, "Invalid model import bundle arguments");
        return NULL;
    }
    graph = (cxpr_model_import_bundle*)calloc(1u, sizeof(*graph));
    if (!graph) {
        import_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }
    builder.bundle = graph;
    builder.load = load;
    builder.userdata = userdata;
    for (i = 0u; i < cxpr_model_use_count(root_model); ++i) {
        cxpr_model_import_node* node;
        cxpr_model_import* grown;
        const char* use_path = cxpr_model_use(root_model, i);
        if (!import_compile(&builder, root_id, use_path, &node, err)) goto fail;
        if (!node) continue;
        grown = (cxpr_model_import*)realloc(
            graph->root_imports,
            (graph->root_import_count + 1u) * sizeof(*grown));
        if (!grown) {
            import_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
            goto fail;
        }
        graph->root_imports = grown;
        graph->root_imports[graph->root_import_count].name =
            node->namespace_name;
        graph->root_imports[graph->root_import_count].program = node->program;
        graph->root_import_count++;
    }
    free(builder.stack);
    return graph;

fail:
    free(builder.stack);
    cxpr_model_import_bundle_free(graph);
    return NULL;
}

void cxpr_model_import_bundle_free(cxpr_model_import_bundle* graph) {
    size_t i;
    if (!graph) return;
    for (i = 0u; i < graph->node_count; ++i) {
        cxpr_model_program_free(graph->nodes[i].program);
        cxpr_model_free(graph->nodes[i].model);
        free(graph->nodes[i].namespace_name);
        free(graph->nodes[i].source);
        free(graph->nodes[i].id);
    }
    free(graph->nodes);
    free(graph->root_imports);
    free(graph);
}

const cxpr_model_import* cxpr_model_import_bundle_root_imports(
    const cxpr_model_import_bundle* graph,
    size_t* out_count) {
    if (out_count) *out_count = graph ? graph->root_import_count : 0u;
    return graph ? graph->root_imports : NULL;
}

size_t cxpr_model_import_bundle_count(const cxpr_model_import_bundle* graph) {
    return graph ? graph->node_count : 0u;
}

const char* cxpr_model_import_bundle_id(
    const cxpr_model_import_bundle* graph,
    size_t index) {
    return graph && index < graph->node_count ? graph->nodes[index].id : NULL;
}
