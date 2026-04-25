#include <cxpr/cxpr.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cxpr_registry_grow(cxpr_registry* reg);
void cxpr_registry_reset_entry(cxpr_func_entry* entry);
void cxpr_registry_clear_owned_entry(cxpr_func_entry* entry);
void cxpr_registry_prepare_entry(cxpr_func_entry* entry, const char* name);
char** cxpr_registry_clone_param_names(const char* const* param_names, size_t param_count);
cxpr_value_type* cxpr_registry_clone_arg_types(const cxpr_value_type* arg_types, size_t arg_count);

static char* test_strdup(const char* s) {
    size_t len;
    char* copy;

    if (!s) return NULL;
    len = strlen(s) + 1;
    copy = (char*)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    return copy;
}

static void test_registry_helper_cloners_and_reset(void) {
    const char* params[] = {"fast", "slow"};
    cxpr_value_type arg_types[] = {CXPR_VALUE_NUMBER, CXPR_VALUE_BOOL};
    char** cloned_names;
    cxpr_value_type* cloned_types;
    cxpr_func_entry entry;
    cxpr_func_entry owned = {0};
    cxpr_registry* reg = cxpr_registry_new();

    cloned_names = cxpr_registry_clone_param_names(params, 2);
    cloned_types = cxpr_registry_clone_arg_types(arg_types, 2);
    assert(reg);
    assert(cloned_names);
    assert(cloned_types);
    assert(strcmp(cloned_names[0], "fast") == 0);
    assert(cloned_types[1] == CXPR_VALUE_BOOL);

    memset(&entry, 0, sizeof(entry));
    cxpr_registry_prepare_entry(&entry, "ema");
    assert(strcmp(entry.name, "ema") == 0);
    cxpr_registry_reset_entry(&entry);
    assert(entry.name == NULL);

    assert(cxpr_registry_grow(reg));

    owned.name = test_strdup("owned");
    assert(owned.name);
    owned.param_names = cloned_names;
    owned.param_name_count = 2;
    owned.arg_types = cloned_types;
    owned.arg_type_count = 2;
    cxpr_registry_clear_owned_entry(&owned);
    cxpr_registry_free(reg);
}

int main(void) {
    test_registry_helper_cloners_and_reset();
    printf("  \xE2\x9C\x93 registry_helpers\n");
    return 0;
}
