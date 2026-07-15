#include <cxpr/document.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct cxpr_document {
    cxpr_model* model;
    unsigned extensions;
};

static void cxpr_document_set_error(cxpr_error* err,
                                    cxpr_error_code code,
                                    const char* message) {
    if (!err) return;
    err->code = code;
    err->message = message;
    err->line = 0u;
    err->column = 0u;
}

static bool cxpr_document_model_constructs_empty(const cxpr_model* model,
                                                 cxpr_error* err) {
    if (!model) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "NULL document model");
        return false;
    }
    if ((cxpr_model_name(model) && cxpr_model_name(model)[0] != '\0') ||
        cxpr_model_use_count(model) != 0u ||
        cxpr_model_input_count(model) != 0u ||
        cxpr_model_constant_count(model) != 0u ||
        cxpr_model_binding_count(model) != 0u ||
        cxpr_model_output_count(model) != 0u ||
        cxpr_model_metadata_count(model) != 0u) {
        cxpr_document_set_error(
            err,
            CXPR_ERR_SYNTAX,
            "Model syntax requires CXPR_DOCUMENT_EXTENSION_MODEL");
        return false;
    }
    return true;
}

cxpr_document* cxpr_parse_document(const char* source,
                                   unsigned extensions,
                                   cxpr_error* err) {
    cxpr_model* model;
    cxpr_document* document;

    if (err) *err = (cxpr_error){0};
    model = cxpr_parse_model(source, err);
    if (!model) return NULL;

    if ((extensions & CXPR_DOCUMENT_EXTENSION_MODEL) == 0u &&
        !cxpr_document_model_constructs_empty(model, err)) {
        cxpr_model_free(model);
        return NULL;
    }

    document = (cxpr_document*)calloc(1u, sizeof(*document));
    if (!document) {
        cxpr_model_free(model);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }
    document->model = model;
    document->extensions = extensions;
    return document;
}

cxpr_document* cxpr_load_document_file(const char* path,
                                       unsigned extensions,
                                       cxpr_error* err) {
    FILE* file;
    long size;
    size_t read_size;
    char* source;
    cxpr_document* document;

    if (err) *err = (cxpr_error){0};
    if (!path) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "NULL document path");
        return NULL;
    }
    file = fopen(path, "rb");
    if (!file) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to open document");
        return NULL;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to read document");
        return NULL;
    }
    size = ftell(file);
    if (size < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to read document");
        return NULL;
    }
    source = (char*)malloc((size_t)size + 1u);
    if (!source) {
        fclose(file);
        cxpr_document_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return NULL;
    }
    read_size = fread(source, 1u, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        free(source);
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "Failed to read document");
        return NULL;
    }
    source[size] = '\0';
    document = cxpr_parse_document(source, extensions, err);
    free(source);
    return document;
}

cxpr_document* cxpr_load_manifest_file(const char* path, cxpr_error* err) {
    return cxpr_load_document_file(path, CXPR_DOCUMENT_EXTENSION_NONE, err);
}

cxpr_document* cxpr_load_model_document_file(const char* path, cxpr_error* err) {
    return cxpr_load_document_file(path, CXPR_DOCUMENT_EXTENSION_MODEL, err);
}

cxpr_document* cxpr_parse_manifest(const char* source, cxpr_error* err) {
    return cxpr_parse_document(source, CXPR_DOCUMENT_EXTENSION_NONE, err);
}

cxpr_document* cxpr_parse_model_document(const char* source, cxpr_error* err) {
    return cxpr_parse_document(source, CXPR_DOCUMENT_EXTENSION_MODEL, err);
}

void cxpr_document_free(cxpr_document* document) {
    if (!document) return;
    cxpr_model_free(document->model);
    free(document);
}

size_t cxpr_document_host_block_count(const cxpr_document* document) {
    return document ? cxpr_model_host_block_count(document->model) : 0u;
}

const cxpr_model_host_block* cxpr_document_host_block_at(const cxpr_document* document,
                                                         size_t index) {
    return document ? cxpr_model_host_block_at(document->model, index) : NULL;
}

const cxpr_model_host_block* cxpr_document_host_block(const cxpr_document* document,
                                                      const char* kind) {
    if (!document || !kind) return NULL;
    for (size_t i = 0u; i < cxpr_model_host_block_count(document->model); ++i) {
        const cxpr_model_host_block* block = cxpr_model_host_block_at(document->model, i);
        const char* block_kind = cxpr_host_block_kind(block);
        if (block_kind && strcmp(block_kind, kind) == 0) return block;
    }
    return NULL;
}

bool cxpr_document_validate_host_blocks(const cxpr_document* document,
                                        const cxpr_host_block_registry* registry,
                                        cxpr_error* err) {
    if (!document) {
        cxpr_document_set_error(err, CXPR_ERR_SYNTAX, "NULL document");
        return false;
    }
    return cxpr_model_validate_host_blocks(document->model, registry, err);
}

const cxpr_model* cxpr_document_model(const cxpr_document* document) {
    if (!document || (document->extensions & CXPR_DOCUMENT_EXTENSION_MODEL) == 0u) {
        return NULL;
    }
    return document->model;
}
