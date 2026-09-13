#include "model/internal.h"

#include <stdlib.h>
#include <string.h>

struct cxpr_host_block_registry {
    cxpr_host_block_spec* specs;
    size_t count;
};

static const cxpr_host_block_spec*
cxpr_host_block_registry_find(const cxpr_host_block_registry* registry,
                              const char* kind) {
    if (!registry || !kind) return NULL;
    for (size_t i = 0u; i < registry->count; ++i) {
        if (registry->specs[i].kind && strcmp(registry->specs[i].kind, kind) == 0) {
            return &registry->specs[i];
        }
    }
    return NULL;
}

static bool cxpr_host_block_kind_is_ident(const char* kind) {
    const unsigned char* cursor = (const unsigned char*)kind;
    if (!cursor || !((*cursor >= 'A' && *cursor <= 'Z') ||
                     (*cursor >= 'a' && *cursor <= 'z') ||
                     *cursor == '_')) {
        return false;
    }
    cursor++;
    while (*cursor) {
        if (!((*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= '0' && *cursor <= '9') ||
              *cursor == '_')) {
            return false;
        }
        cursor++;
    }
    return true;
}

cxpr_host_block_registry* cxpr_host_block_registry_new(void) {
    return (cxpr_host_block_registry*)calloc(1u, sizeof(cxpr_host_block_registry));
}

void cxpr_host_block_registry_free(cxpr_host_block_registry* registry) {
    if (!registry) return;
    free(registry->specs);
    free(registry);
}

bool cxpr_host_block_registry_register(cxpr_host_block_registry* registry,
                                       const cxpr_host_block_spec* spec) {
    cxpr_host_block_spec* grown;

    if (!registry || !spec || !cxpr_host_block_kind_is_ident(spec->kind)) {
        return false;
    }
    if (cxpr_host_block_registry_find(registry, spec->kind)) {
        return false;
    }

    grown = (cxpr_host_block_spec*)realloc(
        registry->specs,
        (registry->count + 1u) * sizeof(cxpr_host_block_spec));
    if (!grown) return false;

    registry->specs = grown;
    registry->specs[registry->count] = *spec;
    registry->count++;
    return true;
}

bool cxpr_model_validate_host_blocks(const cxpr_model* model,
                                     const cxpr_host_block_registry* registry,
                                     cxpr_error* err) {
    if (err) *err = (cxpr_error){0};
    if (!model) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX, "NULL model", 0, 0);
        return false;
    }
    if (model->host_block_count == 0u) {
        if (err) err->code = CXPR_OK;
        return true;
    }
    if (!registry) {
        cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                             "Host block registry is required", 0, 0);
        return false;
    }

    for (size_t i = 0u; i < model->host_block_count; ++i) {
        const cxpr_model_host_block* block = &model->host_blocks[i];
        const cxpr_host_block_spec* spec =
            cxpr_host_block_registry_find(registry, block->kind);

        if (!spec) {
            cxpr_model_set_error(err, CXPR_ERR_UNKNOWN_IDENTIFIER,
                                 "Unknown host block kind", 0, 0);
            return false;
        }
        if (!spec->allow_named && block->name && block->name[0] != '\0') {
            cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                 "Host block kind does not allow names", 0, 0);
            return false;
        }
        if (!spec->allow_multiple) {
            for (size_t j = 0u; j < i; ++j) {
                if (strcmp(model->host_blocks[j].kind, block->kind) == 0) {
                    cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                         "Duplicate host block kind", 0, 0);
                    return false;
                }
            }
        }
        if (spec->validate_block &&
            !spec->validate_block(block, spec->userdata, err)) {
            if (err && err->code == CXPR_OK) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Host block validation failed", 0, 0);
            }
            return false;
        }
        if (!spec->validate_block && spec->validate &&
            !spec->validate(block->kind, block->name, block->body,
                            spec->userdata, err)) {
            if (err && err->code == CXPR_OK) {
                cxpr_model_set_error(err, CXPR_ERR_SYNTAX,
                                     "Host block validation failed", 0, 0);
            }
            return false;
        }
    }

    if (err) err->code = CXPR_OK;
    return true;
}
