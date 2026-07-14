#include "model/internal.h"
#include <stdlib.h>
#include <string.h>

static void cxpr_model_metadatas_free(cxpr_model_metadata* metadatas, size_t count) {
    if (!metadatas) return;
    for (size_t i = 0u; i < count; ++i) {
        free(metadatas[i].name);
        free(metadatas[i].body);
        free(metadatas[i].target_name);
    }
    free(metadatas);
}

static void cxpr_model_host_blocks_free(cxpr_model_host_block* blocks, size_t count) {
    if (!blocks) return;
    for (size_t i = 0u; i < count; ++i) {
        free(blocks[i].kind);
        free(blocks[i].name);
        free(blocks[i].body);
        for (size_t j = 0u; j < blocks[i].field_count; ++j) {
            free(blocks[i].fields[j].key);
            free(blocks[i].fields[j].value);
        }
        free(blocks[i].fields);
        cxpr_model_host_blocks_free(blocks[i].children, blocks[i].child_count);
    }
    free(blocks);
}

void cxpr_model_record_fields_free(cxpr_model_record_field* fields, size_t count) {
    if (!fields) return;
    for (size_t i = 0; i < count; ++i) {
        free(fields[i].name);
        free(fields[i].source);
        cxpr_ast_free(fields[i].expr);
    }
    free(fields);
}

void cxpr_model_record_function_clear(cxpr_model_record_function* fn) {
    if (!fn) return;
    free(fn->name);
    for (size_t i = 0; i < fn->param_count; ++i) free(fn->params[i]);
    free(fn->params);
    cxpr_model_record_fields_free(fn->fields, fn->field_count);
    fn->name = NULL;
    fn->params = NULL;
    fn->param_count = 0u;
    fn->fields = NULL;
    fn->field_count = 0u;
}

void cxpr_model_free(cxpr_model* model) {
    if (!model) return;
    free(model->name);
    for (size_t i = 0; i < model->use_count; ++i) free(model->uses[i]);
    free(model->uses);
    for (size_t i = 0; i < model->function_count; ++i) free(model->functions[i]);
    free(model->functions);
    for (size_t i = 0; i < model->record_function_count; ++i) {
        cxpr_model_record_function_clear(&model->record_functions[i]);
    }
    free(model->record_functions);
    for (size_t i = 0; i < model->input_count; ++i) free(model->inputs[i]);
    free(model->inputs);
    for (size_t i = 0; i < model->constant_count; ++i) {
        free(model->constants[i].name);
        free(model->constants[i].source);
        cxpr_ast_free(model->constants[i].expr);
    }
    free(model->constants);
    for (size_t i = 0; i < model->binding_count; ++i) {
        free(model->bindings[i].name);
        free(model->bindings[i].source);
        cxpr_ast_free(model->bindings[i].expr);
    }
    free(model->bindings);
    for (size_t i = 0; i < model->output_count; ++i) free(model->outputs[i]);
    free(model->outputs);
    cxpr_model_metadatas_free(model->metadatas, model->metadata_count);
    cxpr_model_host_blocks_free(model->host_blocks, model->host_block_count);
    free(model);
}

const char* cxpr_model_name(const cxpr_model* model) {
    return model ? model->name : NULL;
}

size_t cxpr_model_use_count(const cxpr_model* model) {
    return model ? model->use_count : 0;
}

const char* cxpr_model_use(const cxpr_model* model, size_t index) {
    return model && index < model->use_count ? model->uses[index] : NULL;
}

size_t cxpr_model_input_count(const cxpr_model* model) {
    return model ? model->input_count : 0;
}

const char* cxpr_model_input(const cxpr_model* model, size_t index) {
    return model && index < model->input_count ? model->inputs[index] : NULL;
}

size_t cxpr_model_constant_count(const cxpr_model* model) {
    return model ? model->constant_count : 0;
}

const char* cxpr_model_constant_name(const cxpr_model* model, size_t index) {
    return model && index < model->constant_count ? model->constants[index].name : NULL;
}

const cxpr_ast* cxpr_model_constant_expr(const cxpr_model* model, size_t index) {
    return model && index < model->constant_count ? model->constants[index].expr : NULL;
}

size_t cxpr_model_binding_count(const cxpr_model* model) {
    return model ? model->binding_count : 0;
}

cxpr_model_binding_kind cxpr_model_binding_kind_at(const cxpr_model* model, size_t index) {
    if (!model || index >= model->binding_count) return CXPR_MODEL_BINDING_EXPR;
    return model->bindings[index].kind;
}

const char* cxpr_model_binding_name(const cxpr_model* model, size_t index) {
    return model && index < model->binding_count ? model->bindings[index].name : NULL;
}

const cxpr_ast* cxpr_model_binding_expr(const cxpr_model* model, size_t index) {
    return model && index < model->binding_count ? model->bindings[index].expr : NULL;
}

size_t cxpr_model_output_count(const cxpr_model* model) {
    return model ? model->output_count : 0;
}

const char* cxpr_model_output(const cxpr_model* model, size_t index) {
    return model && index < model->output_count ? model->outputs[index] : NULL;
}

size_t cxpr_model_metadata_count(const cxpr_model* model) {
    return model ? model->metadata_count : 0u;
}

const char* cxpr_model_metadata_name(const cxpr_model* model, size_t index) {
    return model && index < model->metadata_count ? model->metadatas[index].name : NULL;
}

const char* cxpr_model_metadata_body(const cxpr_model* model, size_t index) {
    return model && index < model->metadata_count ? model->metadatas[index].body : NULL;
}

cxpr_model_metadata_target_kind cxpr_model_metadata_target_kind_at(
    const cxpr_model* model,
    size_t index) {
    if (!model || index >= model->metadata_count) {
        return CXPR_MODEL_METADATA_TARGET_BINDING;
    }
    return model->metadatas[index].target_kind;
}

const char* cxpr_model_metadata_target_name(const cxpr_model* model, size_t index) {
    return model && index < model->metadata_count ? model->metadatas[index].target_name : NULL;
}

size_t cxpr_model_host_block_count(const cxpr_model* model) {
    return model ? model->host_block_count : 0u;
}

const char* cxpr_model_host_block_kind(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? model->host_blocks[index].kind : NULL;
}

const char* cxpr_model_host_block_name(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? model->host_blocks[index].name : NULL;
}

const char* cxpr_model_host_block_body(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? model->host_blocks[index].body : NULL;
}

const cxpr_model_host_block* cxpr_model_host_block_at(const cxpr_model* model, size_t index) {
    return model && index < model->host_block_count ? &model->host_blocks[index] : NULL;
}

const char* cxpr_host_block_kind(const cxpr_model_host_block* block) {
    return block ? block->kind : NULL;
}

const char* cxpr_host_block_name(const cxpr_model_host_block* block) {
    return block ? block->name : NULL;
}

const char* cxpr_host_block_body(const cxpr_model_host_block* block) {
    return block ? block->body : NULL;
}

size_t cxpr_host_block_field_count(const cxpr_model_host_block* block) {
    return block ? block->field_count : 0u;
}

const char* cxpr_host_block_field_key(const cxpr_model_host_block* block, size_t index) {
    return block && index < block->field_count ? block->fields[index].key : NULL;
}

const char* cxpr_host_block_field_value(const cxpr_model_host_block* block, size_t index) {
    return block && index < block->field_count ? block->fields[index].value : NULL;
}

const char* cxpr_host_block_field_value_by_key(const cxpr_model_host_block* block,
                                               const char* key) {
    if (!block || !key) return NULL;
    for (size_t i = 0u; i < block->field_count; ++i) {
        if (block->fields[i].key && strcmp(block->fields[i].key, key) == 0) {
            return block->fields[i].value;
        }
    }
    return NULL;
}

size_t cxpr_host_block_child_count(const cxpr_model_host_block* block) {
    return block ? block->child_count : 0u;
}

const cxpr_model_host_block* cxpr_host_block_child(const cxpr_model_host_block* block,
                                                   size_t index) {
    return block && index < block->child_count ? &block->children[index] : NULL;
}
