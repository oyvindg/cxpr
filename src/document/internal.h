#ifndef CXPR_DOCUMENT_INTERNAL_H
#define CXPR_DOCUMENT_INTERNAL_H

#include "model/internal.h"

#include <cxpr/doc/ast.h>

bool cxpr_document_names_match(const char* a, const char* b);
void cxpr_document_map_source_spans(cxpr_model* model,
                                    const cxpr_doc_ast* syntax);

#endif /* CXPR_DOCUMENT_INTERNAL_H */
