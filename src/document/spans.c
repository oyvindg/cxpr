/**
 * @file document/spans.c
 * @brief Source-span mapping from document AST nodes to lowered models.
 */

#include "document/internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    cxpr_model* model;
    size_t next_use;
    size_t next_input;
    size_t next_constant;
    size_t next_binding;
    size_t next_output;
    size_t next_host_block;
} cxpr_document_span_mapper;

bool cxpr_document_names_match(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

static void cxpr_doc_model_prepare_span_storage(cxpr_model* model) {
    if (!model) return;
    if (model->use_count > 0u && !model->use_spans) {
        model->use_spans = (cxpr_source_span*)calloc(model->use_count,
                                                     sizeof(*model->use_spans));
        model->use_has_spans = (bool*)calloc(model->use_count,
                                             sizeof(*model->use_has_spans));
    }
    if (model->input_count > 0u && !model->input_spans) {
        model->input_spans = (cxpr_source_span*)calloc(model->input_count,
                                                       sizeof(*model->input_spans));
        model->input_has_spans = (bool*)calloc(model->input_count,
                                               sizeof(*model->input_has_spans));
    }
    if (model->output_count > 0u && !model->output_spans) {
        model->output_spans = (cxpr_source_span*)calloc(model->output_count,
                                                        sizeof(*model->output_spans));
        model->output_has_spans = (bool*)calloc(model->output_count,
                                                sizeof(*model->output_has_spans));
    }
}

static void cxpr_document_map_use_span(cxpr_document_span_mapper* mapper,
                                       const char* text,
                                       cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model || !model->use_spans || !model->use_has_spans) return;
    (void)text;
    if (mapper->next_use < model->use_count) {
        model->use_spans[mapper->next_use] = span;
        model->use_has_spans[mapper->next_use] = true;
        mapper->next_use++;
    }
}

static void cxpr_document_map_input_span(cxpr_document_span_mapper* mapper,
                                         const char* name,
                                         cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model || !model->input_spans || !model->input_has_spans) return;
    for (size_t i = mapper->next_input; i < model->input_count; ++i) {
        if (!cxpr_document_names_match(model->inputs[i], name)) continue;
        model->input_spans[i] = span;
        model->input_has_spans[i] = true;
        mapper->next_input = i + 1u;
        return;
    }
}

static void cxpr_document_map_constant_span(cxpr_document_span_mapper* mapper,
                                            const char* name,
                                            cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model) return;
    for (size_t i = mapper->next_constant; i < model->constant_count; ++i) {
        if (!cxpr_document_names_match(model->constants[i].name, name)) continue;
        model->constants[i].span = span;
        model->constants[i].has_span = true;
        mapper->next_constant = i + 1u;
        return;
    }
}

static void cxpr_document_map_binding_span(cxpr_document_span_mapper* mapper,
                                           const char* name,
                                           cxpr_model_binding_kind kind,
                                           cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model) return;
    for (size_t i = mapper->next_binding; i < model->binding_count; ++i) {
        if (model->bindings[i].kind != kind ||
            !cxpr_document_names_match(model->bindings[i].name, name)) {
            continue;
        }
        model->bindings[i].span = span;
        model->bindings[i].has_span = true;
        mapper->next_binding = i + 1u;
        return;
    }
}

static void cxpr_document_map_output_span(cxpr_document_span_mapper* mapper,
                                          const char* name,
                                          cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model || !model->output_spans || !model->output_has_spans) return;
    for (size_t i = mapper->next_output; i < model->output_count; ++i) {
        if (!cxpr_document_names_match(model->outputs[i], name)) continue;
        model->output_spans[i] = span;
        model->output_has_spans[i] = true;
        mapper->next_output = i + 1u;
        return;
    }
}

static void cxpr_document_map_host_block_span(cxpr_document_span_mapper* mapper,
                                              const char* kind,
                                              cxpr_source_span span) {
    cxpr_model* model = mapper->model;
    if (!model) return;
    for (size_t i = mapper->next_host_block; i < model->host_block_count; ++i) {
        if (!cxpr_document_names_match(model->host_blocks[i].kind, kind)) continue;
        model->host_blocks[i].span = span;
        model->host_blocks[i].has_span = true;
        mapper->next_host_block = i + 1u;
        return;
    }
}

static cxpr_visit_control cxpr_document_map_source_span_node(
    const cxpr_doc_ast_node* node,
    void* userdata) {
    cxpr_document_span_mapper* mapper = (cxpr_document_span_mapper*)userdata;
    const char* name = cxpr_doc_ast_node_name(node);
    cxpr_source_span span = cxpr_doc_ast_node_span(node);

    switch (cxpr_doc_ast_node_kind(node)) {
        case CXPR_DOC_AST_HOST_BLOCK:
            cxpr_document_map_host_block_span(mapper, name, span);
            break;
        case CXPR_DOC_AST_MODEL_DECL:
            if (mapper->model && cxpr_document_names_match(mapper->model->name, name)) {
                mapper->model->name_span = span;
                mapper->model->has_name_span = true;
            }
            break;
        case CXPR_DOC_AST_USE:
            cxpr_document_map_use_span(mapper, cxpr_doc_ast_node_text(node), span);
            break;
        case CXPR_DOC_AST_INPUT_DECL:
            cxpr_document_map_input_span(mapper, name, span);
            break;
        case CXPR_DOC_AST_PARAM_DECL:
            cxpr_document_map_constant_span(mapper, name, span);
            break;
        case CXPR_DOC_AST_STATE_DECL:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE, span);
            break;
        case CXPR_DOC_AST_STATE_UPDATE:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE_UPDATE, span);
            break;
        case CXPR_DOC_AST_INITIAL_STATE_UPDATE:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE_UPDATE, span);
            break;
        case CXPR_DOC_AST_BINDING:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_EXPR, span);
            break;
        case CXPR_DOC_AST_OUTPUT_STATE_UPDATE:
            cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_STATE_UPDATE, span);
            cxpr_document_map_output_span(mapper, name, span);
            break;
        case CXPR_DOC_AST_OUTPUT_DECL:
            if (cxpr_doc_ast_node_expr(node)) {
                cxpr_document_map_binding_span(mapper, name, CXPR_MODEL_BINDING_EXPR, span);
            }
            cxpr_document_map_output_span(mapper, name, span);
            break;
        default:
            break;
    }
    return CXPR_VISIT_CONTINUE;
}

static bool cxpr_document_span_for_metadata_target(const cxpr_model* model,
                                                   const cxpr_model_metadata* metadata,
                                                   cxpr_source_span* out_span) {
    if (!model || !metadata || !out_span) return false;
    switch (metadata->target_kind) {
        case CXPR_MODEL_METADATA_TARGET_MODEL:
            if (model->has_name_span) {
                *out_span = model->name_span;
                return true;
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_USE:
            for (size_t i = 0u; i < model->use_count; ++i) {
                if (model->use_has_spans && model->use_has_spans[i]) {
                    *out_span = model->use_spans[i];
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_INPUT:
            for (size_t i = 0u; i < model->input_count; ++i) {
                if (cxpr_document_names_match(model->inputs[i], metadata->target_name) &&
                    model->input_has_spans && model->input_has_spans[i]) {
                    *out_span = model->input_spans[i];
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_PARAM:
            for (size_t i = 0u; i < model->constant_count; ++i) {
                if (cxpr_document_names_match(model->constants[i].name, metadata->target_name) &&
                    model->constants[i].has_span) {
                    *out_span = model->constants[i].span;
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_STATE:
            for (size_t i = 0u; i < model->binding_count; ++i) {
                if (model->bindings[i].kind == CXPR_MODEL_BINDING_STATE &&
                    cxpr_document_names_match(model->bindings[i].name, metadata->target_name) &&
                    model->bindings[i].has_span) {
                    *out_span = model->bindings[i].span;
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_BINDING:
            for (size_t i = 0u; i < model->binding_count; ++i) {
                if (cxpr_document_names_match(model->bindings[i].name, metadata->target_name) &&
                    model->bindings[i].has_span) {
                    *out_span = model->bindings[i].span;
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_OUTPUT:
            for (size_t i = 0u; i < model->output_count; ++i) {
                if (cxpr_document_names_match(model->outputs[i], metadata->target_name) &&
                    model->output_has_spans && model->output_has_spans[i]) {
                    *out_span = model->output_spans[i];
                    return true;
                }
            }
            return false;
        case CXPR_MODEL_METADATA_TARGET_FUNCTION:
        default:
            return false;
    }
}

static void cxpr_document_map_metadata_spans(cxpr_model* model) {
    if (!model) return;
    for (size_t i = 0u; i < model->metadata_count; ++i) {
        cxpr_source_span span;
        if (cxpr_document_span_for_metadata_target(model, &model->metadatas[i], &span)) {
            model->metadatas[i].span = span;
            model->metadatas[i].has_span = true;
        }
    }
}

void cxpr_document_map_source_spans(cxpr_model* model,
                                           const cxpr_doc_ast* syntax) {
    cxpr_document_span_mapper mapper;
    if (!model || !syntax) return;
    cxpr_doc_model_prepare_span_storage(model);
    mapper = (cxpr_document_span_mapper){0};
    mapper.model = model;
    (void)cxpr_doc_ast_visit(syntax, cxpr_document_map_source_span_node, &mapper);
    cxpr_document_map_metadata_spans(model);
}
