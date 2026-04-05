/**
 * @file context_struct.c
 * @brief Typed struct storage support for cxpr contexts.
 */

#include "internal.h"

static cxpr_value cxpr_value_clone(const cxpr_value* value);

/* ═══════════════════════════════════════════════════════════════════════════
 * Typed struct values
 * ═══════════════════════════════════════════════════════════════════════════ */

static void cxpr_struct_value_reset(cxpr_struct_value* s) {
    if (!s) return;
    for (size_t i = 0; i < s->field_count; i++) {
        free((char*)s->field_names[i]);
        if (s->field_values[i].type == CXPR_VALUE_STRUCT) {
            cxpr_struct_value_free(s->field_values[i].s);
        }
    }
    free(s->field_names);
    free(s->field_values);
    s->field_names = NULL;
    s->field_values = NULL;
    s->field_count = 0;
}

static cxpr_value cxpr_value_clone(const cxpr_value* value) {
    if (!value) return cxpr_fv_double(0.0);

    switch (value->type) {
    case CXPR_VALUE_NUMBER:
        return cxpr_fv_double(value->d);
    case CXPR_VALUE_BOOL:
        return cxpr_fv_bool(value->b);
    case CXPR_VALUE_STRUCT:
        return cxpr_fv_struct(cxpr_struct_value_new(
            value->s ? (const char* const*)value->s->field_names : NULL,
            value->s ? value->s->field_values : NULL,
            value->s ? value->s->field_count : 0));
    default:
        return cxpr_fv_double(0.0);
    }
}

cxpr_struct_value* cxpr_struct_value_new(const char* const* field_names,
                                         const cxpr_value* field_values,
                                         size_t field_count) {
    cxpr_struct_value* s = (cxpr_struct_value*)calloc(1, sizeof(cxpr_struct_value));
    if (!s) return NULL;

    s->field_count = field_count;
    if (field_count == 0) return s;

    s->field_names = (const char**)calloc(field_count, sizeof(char*));
    s->field_values = (cxpr_value*)calloc(field_count, sizeof(cxpr_value));
    if (!s->field_names || !s->field_values) {
        cxpr_struct_value_free(s);
        return NULL;
    }

    for (size_t i = 0; i < field_count; i++) {
        s->field_names[i] = cxpr_strdup(field_names[i]);
        if (!s->field_names[i]) {
            cxpr_struct_value_free(s);
            return NULL;
        }
        s->field_values[i] = cxpr_value_clone(&field_values[i]);
        if (field_values[i].type == CXPR_VALUE_STRUCT && field_values[i].s &&
            !s->field_values[i].s) {
            cxpr_struct_value_free(s);
            return NULL;
        }
    }

    return s;
}

void cxpr_struct_value_free(cxpr_struct_value* s) {
    if (!s) return;
    cxpr_struct_value_reset(s);
    free(s);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Struct map
 * ═══════════════════════════════════════════════════════════════════════════ */

static cxpr_struct_map_entry* cxpr_struct_map_find_slot(const cxpr_struct_map* map,
                                                        const char* name) {
    unsigned long hash;

    if (!map->entries || map->capacity == 0) return NULL;

    hash = cxpr_hash_string(name) % map->capacity;
    while (map->entries[hash].name) {
        if (strcmp(map->entries[hash].name, name) == 0) {
            return &((cxpr_struct_map*)map)->entries[hash];
        }
        hash = (hash + 1) % map->capacity;
    }
    return &((cxpr_struct_map*)map)->entries[hash];
}

static bool cxpr_struct_map_grow(cxpr_struct_map* map) {
    if (map->capacity > SIZE_MAX / 2) return false;
    size_t new_capacity = map->capacity * 2;
    cxpr_struct_map_entry* new_entries =
        (cxpr_struct_map_entry*)calloc(new_capacity, sizeof(cxpr_struct_map_entry));
    if (!new_entries) return false;

    for (size_t i = 0; i < map->capacity; i++) {
        if (!map->entries[i].name) continue;
        unsigned long hash = cxpr_hash_string(map->entries[i].name) % new_capacity;
        while (new_entries[hash].name) {
            hash = (hash + 1) % new_capacity;
        }
        new_entries[hash] = map->entries[i];
    }

    free(map->entries);
    map->entries = new_entries;
    map->capacity = new_capacity;
    return true;
}

void cxpr_struct_map_init(cxpr_struct_map* map) {
    map->capacity = 0;
    map->count = 0;
    map->entries = NULL;
}

void cxpr_struct_map_destroy(cxpr_struct_map* map) {
    if (!map->entries) return;
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        cxpr_struct_value_free(map->entries[i].value);
    }
    free(map->entries);
    map->entries = NULL;
    map->capacity = 0;
    map->count = 0;
}

void cxpr_struct_map_clear(cxpr_struct_map* map) {
    for (size_t i = 0; i < map->capacity; i++) {
        free(map->entries[i].name);
        map->entries[i].name = NULL;
        cxpr_struct_value_free(map->entries[i].value);
        map->entries[i].value = NULL;
    }
    map->count = 0;
}

bool cxpr_struct_map_clone(cxpr_struct_map* dst, const cxpr_struct_map* src) {
    cxpr_struct_map_init(dst);
    if (!src || !src->entries || src->count == 0) return true;

    dst->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
    dst->entries = (cxpr_struct_map_entry*)calloc(dst->capacity, sizeof(cxpr_struct_map_entry));
    if (!dst->entries) return false;

    for (size_t i = 0; i < src->capacity; i++) {
        cxpr_struct_value* copy;
        cxpr_struct_map_entry* slot;
        if (!src->entries[i].name) continue;
        copy = cxpr_struct_value_new((const char* const*)src->entries[i].value->field_names,
                                     src->entries[i].value->field_values,
                                     src->entries[i].value->field_count);
        if (!copy) return false;
        if ((double)(dst->count + 1) / dst->capacity > CXPR_HASHMAP_LOAD_FACTOR
            && !cxpr_struct_map_grow(dst)) {
            cxpr_struct_value_free(copy);
            return false;
        }
        slot = cxpr_struct_map_find_slot(dst, src->entries[i].name);
        slot->name = cxpr_strdup(src->entries[i].name);
        slot->value = copy;
        if (!slot->name) return false;
        dst->count++;
    }
    return true;
}

static const cxpr_struct_map_entry* cxpr_struct_map_get(const cxpr_struct_map* map,
                                                        const char* name) {
    cxpr_struct_map_entry* slot = cxpr_struct_map_find_slot(map, name);
    if (!slot || !slot->name) return NULL;
    return slot;
}

void cxpr_context_store_struct(cxpr_struct_map* map, const char* name,
                               const cxpr_struct_value* value) {
    cxpr_struct_map_entry* slot;
    cxpr_struct_value* copy;

    if (!map || !name || !value) return;

    copy = cxpr_struct_value_new((const char* const*)value->field_names,
                                 value->field_values, value->field_count);
    if (!copy) return;

    if (!map->entries) {
        map->capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
        map->entries =
            (cxpr_struct_map_entry*)calloc(map->capacity, sizeof(cxpr_struct_map_entry));
        if (!map->entries) {
            map->capacity = 0;
            cxpr_struct_value_free(copy);
            return;
        }
    }

    if ((double)(map->count + 1) / map->capacity > CXPR_HASHMAP_LOAD_FACTOR
        && !cxpr_struct_map_grow(map)) {
        cxpr_struct_value_free(copy);
        return;
    }

    slot = cxpr_struct_map_find_slot(map, name);
    if (slot->name) {
        cxpr_struct_value_free(slot->value);
        slot->value = copy;
        return;
    }

    slot->name = cxpr_strdup(name);
    if (!slot->name) {
        cxpr_struct_value_free(copy);
        return;
    }
    slot->value = copy;
    map->count++;
}

const cxpr_struct_value* cxpr_context_lookup_struct_map(const cxpr_struct_map* map,
                                                        const char* name) {
    const cxpr_struct_map_entry* entry;

    if (!map || !name) return NULL;

    entry = cxpr_struct_map_get(map, name);
    return entry ? entry->value : NULL;
}
