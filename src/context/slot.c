/**
 * @file slot.c
 * @brief Bound-slot helpers for hot-loop variable writes.
 */

#include "internal.h"

bool cxpr_context_slot_bind(cxpr_context* ctx, const char* name, cxpr_context_slot* slot) {
    unsigned long hash;
    cxpr_hashmap_entry* entry;

    if (!slot) return false;
    slot->_ptr = NULL;
    slot->_base = NULL;
    if (!ctx || !name) return false;

    hash = cxpr_hash_string(name);
    entry = cxpr_context_find_variable_slot(ctx, name, hash);
    if (!entry) return false;

    slot->_ptr = &entry->value;
    slot->_base = cxpr_context_variables_base(ctx);
    return true;
}

bool cxpr_context_slots_bind(cxpr_context* ctx, const char* const* names,
                             cxpr_context_slot* slots, size_t count) {
    size_t i;

    if (!names || !slots) return count == 0;

    for (i = 0; i < count; i++) {
        slots[i]._ptr = NULL;
        slots[i]._base = NULL;
    }

    for (i = 0; i < count; i++) {
        if (!cxpr_context_slot_bind(ctx, names[i], &slots[i])) {
            size_t j;
            for (j = 0; j < count; j++) {
                slots[j]._ptr = NULL;
                slots[j]._base = NULL;
            }
            return false;
        }
    }

    return true;
}

bool cxpr_context_slot_valid(const cxpr_context* ctx, const cxpr_context_slot* slot) {
    return ctx && slot && slot->_ptr && slot->_base == cxpr_context_variables_base(ctx);
}

void cxpr_context_slot_set(cxpr_context_slot* slot, double value) {
    *slot->_ptr = value;
}

void cxpr_context_slots_set(cxpr_context_slot* slots, const double* values, size_t count) {
    size_t i;

    for (i = 0; i < count; i++) {
        slots[i]._ptr[0] = values[i];
    }
}

double cxpr_context_slot_get(const cxpr_context_slot* slot) {
    return *slot->_ptr;
}
