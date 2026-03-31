/**
 * @file ast.c
 * @brief AST construction, inspection, and memory management.
 *
 * Provides constructors for each AST node type, the inspection API
 * for codegen tree-walking, and the reference extraction API.
 */

#include "internal.h"
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * AST construction helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

cxpr_ast* cxpr_ast_new_number(double value) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_NUMBER;
    node->data.number.value = value;
    return node;
}

cxpr_ast* cxpr_ast_new_bool(bool value) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_BOOL;
    node->data.boolean.value = value;
    return node;
}

cxpr_ast* cxpr_ast_new_identifier(const char* name) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_IDENTIFIER;
    node->data.identifier.name = cxpr_strdup(name);
    if (!node->data.identifier.name) { free(node); return NULL; }
    return node;
}

cxpr_ast* cxpr_ast_new_variable(const char* name) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_VARIABLE;
    node->data.variable.name = cxpr_strdup(name);
    if (!node->data.variable.name) { free(node); return NULL; }
    return node;
}

cxpr_ast* cxpr_ast_new_field_access(const char* object, const char* field) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_FIELD_ACCESS;
    node->data.field_access.object = cxpr_strdup(object);
    node->data.field_access.field = cxpr_strdup(field);

    /* Build full key "object.field" */
    size_t obj_len = strlen(object);
    size_t fld_len = strlen(field);
    node->data.field_access.full_key = (char*)malloc(obj_len + 1 + fld_len + 1);
    if (node->data.field_access.full_key) {
        memcpy(node->data.field_access.full_key, object, obj_len);
        node->data.field_access.full_key[obj_len] = '.';
        memcpy(node->data.field_access.full_key + obj_len + 1, field, fld_len);
        node->data.field_access.full_key[obj_len + 1 + fld_len] = '\0';
    }

    if (!node->data.field_access.object || !node->data.field_access.field
        || !node->data.field_access.full_key) {
        cxpr_ast_free(node);
        return NULL;
    }
    return node;
}

cxpr_ast* cxpr_ast_new_chain_access(const char* const* path, size_t depth) {
    cxpr_ast* node;
    size_t total_len = 0;

    if (!path || depth < 2) return NULL;

    node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_CHAIN_ACCESS;
    node->data.chain_access.path = (char**)calloc(depth, sizeof(char*));
    if (!node->data.chain_access.path) {
        free(node);
        return NULL;
    }
    node->data.chain_access.depth = depth;

    for (size_t i = 0; i < depth; i++) {
        node->data.chain_access.path[i] = cxpr_strdup(path[i]);
        if (!node->data.chain_access.path[i]) {
            cxpr_ast_free(node);
            return NULL;
        }
        total_len += strlen(path[i]) + 1;
    }

    node->data.chain_access.full_key = (char*)malloc(total_len);
    if (!node->data.chain_access.full_key) {
        cxpr_ast_free(node);
        return NULL;
    }

    node->data.chain_access.full_key[0] = '\0';
    for (size_t i = 0; i < depth; i++) {
        if (i > 0) strcat(node->data.chain_access.full_key, ".");
        strcat(node->data.chain_access.full_key, path[i]);
    }
    return node;
}

cxpr_ast* cxpr_ast_new_producer_access(const char* name, cxpr_ast** args, size_t argc,
                                       const char* field) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    size_t name_len;
    size_t field_len;
    if (!node) return NULL;
    node->type = CXPR_NODE_PRODUCER_ACCESS;
    node->data.producer_access.name = cxpr_strdup(name);
    node->data.producer_access.args = args;
    node->data.producer_access.argc = argc;
    node->data.producer_access.field = cxpr_strdup(field);
    name_len = strlen(name);
    field_len = strlen(field);
    node->data.producer_access.full_key = (char*)malloc(name_len + 1 + field_len + 1);
    if (node->data.producer_access.full_key) {
        memcpy(node->data.producer_access.full_key, name, name_len);
        node->data.producer_access.full_key[name_len] = '.';
        memcpy(node->data.producer_access.full_key + name_len + 1, field, field_len);
        node->data.producer_access.full_key[name_len + 1 + field_len] = '\0';
    }
    if (!node->data.producer_access.name || !node->data.producer_access.field ||
        !node->data.producer_access.full_key) {
        cxpr_ast_free(node);
        return NULL;
    }
    return node;
}

/**
 * @brief Create a BINARY_OP AST node.
 * @param op Operator token (e.g. CXPR_TOK_PLUS, CXPR_TOK_AND).
 * @param left Left child AST (ownership transferred).
 * @param right Right child AST (ownership transferred).
 * @return New AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_binary_op(int op, cxpr_ast* left, cxpr_ast* right) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_BINARY_OP;
    node->data.binary_op.op = op;
    node->data.binary_op.left = left;
    node->data.binary_op.right = right;
    return node;
}

/**
 * @brief Create a UNARY_OP AST node.
 * @param op Operator token (e.g. CXPR_TOK_MINUS, CXPR_TOK_NOT).
 * @param operand Operand child AST (ownership transferred).
 * @return New AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_unary_op(int op, cxpr_ast* operand) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_UNARY_OP;
    node->data.unary_op.op = op;
    node->data.unary_op.operand = operand;
    return node;
}

/**
 * @brief Create a FUNCTION_CALL AST node.
 * @param name Function name (strdup'd internally).
 * @param args Array of argument AST nodes (ownership taken; caller must not free).
 * @param argc Number of arguments.
 * @return New AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_function_call(const char* name, cxpr_ast** args, size_t argc) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_FUNCTION_CALL;
    node->data.function_call.name = cxpr_strdup(name);
    node->data.function_call.args = args;
    node->data.function_call.argc = argc;
    if (!node->data.function_call.name) { free(node); return NULL; }
    return node;
}

/**
 * @brief Create a TERNARY AST node.
 * @param condition Condition expression AST (ownership transferred).
 * @param true_branch True-branch expression AST (ownership transferred).
 * @param false_branch False-branch expression AST (ownership transferred).
 * @return New AST node, or NULL on allocation failure.
 */
cxpr_ast* cxpr_ast_new_ternary(cxpr_ast* condition, cxpr_ast* true_branch, cxpr_ast* false_branch) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_TERNARY;
    node->data.ternary.condition = condition;
    node->data.ternary.true_branch = true_branch;
    node->data.ternary.false_branch = false_branch;
    return node;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AST freeing
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Recursively free an AST and all owned children.
 * @param ast AST to free; safe to pass NULL.
 */
void cxpr_ast_free(cxpr_ast* ast) {
    if (!ast) return;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        break;
    case CXPR_NODE_BOOL:
        break;
    case CXPR_NODE_IDENTIFIER:
        free(ast->data.identifier.name);
        break;
    case CXPR_NODE_VARIABLE:
        free(ast->data.variable.name);
        break;
    case CXPR_NODE_FIELD_ACCESS:
        free(ast->data.field_access.object);
        free(ast->data.field_access.field);
        free(ast->data.field_access.full_key);
        break;
    case CXPR_NODE_CHAIN_ACCESS:
        for (size_t i = 0; i < ast->data.chain_access.depth; i++) {
            free(ast->data.chain_access.path[i]);
        }
        free(ast->data.chain_access.path);
        free(ast->data.chain_access.full_key);
        break;
    case CXPR_NODE_PRODUCER_ACCESS:
        free(ast->data.producer_access.name);
        free(ast->data.producer_access.field);
        free(ast->data.producer_access.full_key);
        free(ast->data.producer_access.cached_const_key);
        for (size_t i = 0; i < ast->data.producer_access.argc; i++) {
            cxpr_ast_free(ast->data.producer_access.args[i]);
        }
        free(ast->data.producer_access.args);
        break;
    case CXPR_NODE_BINARY_OP:
        cxpr_ast_free(ast->data.binary_op.left);
        cxpr_ast_free(ast->data.binary_op.right);
        break;
    case CXPR_NODE_UNARY_OP:
        cxpr_ast_free(ast->data.unary_op.operand);
        break;
    case CXPR_NODE_FUNCTION_CALL:
        free(ast->data.function_call.name);
        free(ast->data.function_call.cached_const_key);
        for (size_t i = 0; i < ast->data.function_call.argc; i++) {
            cxpr_ast_free(ast->data.function_call.args[i]);
        }
        free(ast->data.function_call.args);
        break;
    case CXPR_NODE_TERNARY:
        cxpr_ast_free(ast->data.ternary.condition);
        cxpr_ast_free(ast->data.ternary.true_branch);
        cxpr_ast_free(ast->data.ternary.false_branch);
        break;
    }
    free(ast);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * AST Inspection API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Get the node type of an AST.
 * @param ast AST node (may be NULL).
 * @return Node type, or CXPR_NODE_NUMBER if ast is NULL.
 */
cxpr_node_type cxpr_ast_type(const cxpr_ast* ast) {
    return ast ? ast->type : CXPR_NODE_NUMBER;
}

/**
 * @brief Get numeric value of a NUMBER node.
 * @param ast AST node (must be CXPR_NODE_NUMBER).
 * @return Value, or 0.0 if ast is NULL or wrong type.
 */
double cxpr_ast_number_value(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_NUMBER) ? ast->data.number.value : 0.0;
}

/**
 * @brief Get boolean value of a BOOL node.
 * @param ast AST node (must be CXPR_NODE_BOOL).
 * @return Value, or false if ast is NULL or wrong type.
 */
bool cxpr_ast_bool_value(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_BOOL) ? ast->data.boolean.value : false;
}

/**
 * @brief Get identifier name of an IDENTIFIER node.
 * @param ast AST node (must be CXPR_NODE_IDENTIFIER).
 * @return Name, or NULL if ast is NULL or wrong type.
 */
const char* cxpr_ast_identifier_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_IDENTIFIER) ? ast->data.identifier.name : NULL;
}

/**
 * @brief Get variable name of a VARIABLE node.
 * @param ast AST node (must be CXPR_NODE_VARIABLE).
 * @return Name, or NULL if ast is NULL or wrong type.
 */
const char* cxpr_ast_variable_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_VARIABLE) ? ast->data.variable.name : NULL;
}

/**
 * @brief Get object name of a FIELD_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_FIELD_ACCESS).
 * @return Object name, or NULL if ast is NULL or wrong type.
 */
const char* cxpr_ast_field_object(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FIELD_ACCESS) ? ast->data.field_access.object : NULL;
}

/**
 * @brief Get field name of a FIELD_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_FIELD_ACCESS).
 * @return Field name, or NULL if ast is NULL or wrong type.
 */
const char* cxpr_ast_field_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FIELD_ACCESS) ? ast->data.field_access.field : NULL;
}

size_t cxpr_ast_chain_depth(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_CHAIN_ACCESS) ? ast->data.chain_access.depth : 0;
}

const char* cxpr_ast_chain_segment(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_CHAIN_ACCESS) return NULL;
    if (index >= ast->data.chain_access.depth) return NULL;
    return ast->data.chain_access.path[index];
}

/**
 * @brief Get operator of a BINARY_OP or UNARY_OP node.
 * @param ast AST node (must be CXPR_NODE_BINARY_OP or CXPR_NODE_UNARY_OP).
 * @return Operator token, or 0 if ast is NULL or wrong type.
 */
int cxpr_ast_operator(const cxpr_ast* ast) {
    if (!ast) return 0;
    if (ast->type == CXPR_NODE_BINARY_OP) return ast->data.binary_op.op;
    if (ast->type == CXPR_NODE_UNARY_OP) return ast->data.unary_op.op;
    return 0;
}

/**
 * @brief Get left child of a BINARY_OP node.
 * @param ast AST node (must be CXPR_NODE_BINARY_OP).
 * @return Left child, or NULL if ast is NULL or wrong type.
 */
const cxpr_ast* cxpr_ast_left(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_BINARY_OP) ? ast->data.binary_op.left : NULL;
}

/**
 * @brief Get right child of a BINARY_OP node.
 * @param ast AST node (must be CXPR_NODE_BINARY_OP).
 * @return Right child, or NULL if ast is NULL or wrong type.
 */
const cxpr_ast* cxpr_ast_right(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_BINARY_OP) ? ast->data.binary_op.right : NULL;
}

/**
 * @brief Get operand of a UNARY_OP node.
 * @param ast AST node (must be CXPR_NODE_UNARY_OP).
 * @return Operand child, or NULL if ast is NULL or wrong type.
 */
const cxpr_ast* cxpr_ast_operand(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_UNARY_OP) ? ast->data.unary_op.operand : NULL;
}

/**
 * @brief Get function name of a FUNCTION_CALL node.
 * @param ast AST node (must be CXPR_NODE_FUNCTION_CALL).
 * @return Function name, or NULL if ast is NULL or wrong type.
 */
const char* cxpr_ast_function_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FUNCTION_CALL) ? ast->data.function_call.name : NULL;
}

/**
 * @brief Get argument count of a FUNCTION_CALL node.
 * @param ast AST node (must be CXPR_NODE_FUNCTION_CALL).
 * @return Argument count, or 0 if ast is NULL or wrong type.
 */
size_t cxpr_ast_function_argc(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_FUNCTION_CALL) ? ast->data.function_call.argc : 0;
}

/**
 * @brief Get argument at index of a FUNCTION_CALL node.
 * @param ast AST node (must be CXPR_NODE_FUNCTION_CALL).
 * @param index Argument index (0-based).
 * @return Argument AST, or NULL if ast is NULL, wrong type, or index out of range.
 */
const cxpr_ast* cxpr_ast_function_arg(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_FUNCTION_CALL) return NULL;
    if (index >= ast->data.function_call.argc) return NULL;
    return ast->data.function_call.args[index];
}
/**
 * @brief Get producer name of a PRODUCER_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_PRODUCER_ACCESS).
 * @return Producer name, or NULL if ast is NULL or wrong type.
 */
const char* cxpr_ast_producer_name(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_PRODUCER_ACCESS) ? ast->data.producer_access.name : NULL;
}

/**
 * @brief Get output field of a PRODUCER_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_PRODUCER_ACCESS).
 * @return Output field name, or NULL if ast is NULL or wrong type.
 */
const char* cxpr_ast_producer_field(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_PRODUCER_ACCESS) ? ast->data.producer_access.field : NULL;
}

/**
 * @brief Get argument count of a PRODUCER_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_PRODUCER_ACCESS).
 * @return Argument count, or 0 if ast is NULL or wrong type.
 */
size_t cxpr_ast_producer_argc(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_PRODUCER_ACCESS) ? ast->data.producer_access.argc : 0;
}

/**
 * @brief Get argument at index of a PRODUCER_ACCESS node.
 * @param ast AST node (must be CXPR_NODE_PRODUCER_ACCESS).
 * @param index Argument index (0-based).
 * @return Argument AST, or NULL if ast is NULL, wrong type, or index out of range.
 */
const cxpr_ast* cxpr_ast_producer_arg(const cxpr_ast* ast, size_t index) {
    if (!ast || ast->type != CXPR_NODE_PRODUCER_ACCESS) return NULL;
    if (index >= ast->data.producer_access.argc) return NULL;
    return ast->data.producer_access.args[index];
}
/**
 * @brief Get condition of a TERNARY node.
 * @param ast AST node (must be CXPR_NODE_TERNARY).
 * @return Condition AST, or NULL if ast is NULL or wrong type.
 */
const cxpr_ast* cxpr_ast_ternary_condition(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_TERNARY) ? ast->data.ternary.condition : NULL;
}

/**
 * @brief Get true-branch of a TERNARY node.
 * @param ast AST node (must be CXPR_NODE_TERNARY).
 * @return True-branch AST, or NULL if ast is NULL or wrong type.
 */
const cxpr_ast* cxpr_ast_ternary_true_branch(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_TERNARY) ? ast->data.ternary.true_branch : NULL;
}

/**
 * @brief Get false-branch of a TERNARY node.
 * @param ast AST node (must be CXPR_NODE_TERNARY).
 * @return False-branch AST, or NULL if ast is NULL or wrong type.
 */
const cxpr_ast* cxpr_ast_ternary_false_branch(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_TERNARY) ? ast->data.ternary.false_branch : NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Reference Extraction API
 *
 * These walk the AST tree and collect unique names. Used by codegen
 * to determine which indicators, functions, and $params an expression needs.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Check if a name is already in the array (deduplication).
 */
static bool cxpr_name_in_array(const char** names, size_t count, const char* name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(names[i], name) == 0) return true;
    }
    return false;
}

/**
 * @brief Add a name to the array if unique.
 * @return Updated count.
 */
static size_t cxpr_add_unique_name(const char** names, size_t count, size_t max_names,
                                  const char* name) {
    if (cxpr_name_in_array(names, count < max_names ? count : max_names, name)) {
        return count;
    }
    if (count < max_names) {
        names[count] = name;
    }
    return count + 1;
}

/**
 * @brief Recursive helper collecting identifier and field access names from AST.
 * @param ast AST subtree to traverse.
 * @param names Output array of unique names (pointers into AST).
 * @param count Current number of names collected.
 * @param max_names Maximum capacity of names array.
 * @return Updated count after collection.
 */
static size_t cxpr_collect_references(const cxpr_ast* ast, const char** names,
                                     size_t count, size_t max_names) {
    if (!ast) return count;

    switch (ast->type) {
    case CXPR_NODE_IDENTIFIER:
        return cxpr_add_unique_name(names, count, max_names, ast->data.identifier.name);
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_add_unique_name(names, count, max_names, ast->data.field_access.full_key);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_add_unique_name(names, count, max_names, ast->data.chain_access.full_key);
    case CXPR_NODE_PRODUCER_ACCESS:
        count = cxpr_add_unique_name(names, count, max_names, ast->data.producer_access.full_key);
        for (size_t i = 0; i < ast->data.producer_access.argc; i++) {
            count = cxpr_collect_references(ast->data.producer_access.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_BINARY_OP:
        count = cxpr_collect_references(ast->data.binary_op.left, names, count, max_names);
        count = cxpr_collect_references(ast->data.binary_op.right, names, count, max_names);
        return count;
    case CXPR_NODE_UNARY_OP:
        return cxpr_collect_references(ast->data.unary_op.operand, names, count, max_names);
    case CXPR_NODE_FUNCTION_CALL:
        /* Collect references from function arguments (not the function name itself) */
        for (size_t i = 0; i < ast->data.function_call.argc; i++) {
            count = cxpr_collect_references(ast->data.function_call.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_TERNARY:
        count = cxpr_collect_references(ast->data.ternary.condition, names, count, max_names);
        count = cxpr_collect_references(ast->data.ternary.true_branch, names, count, max_names);
        count = cxpr_collect_references(ast->data.ternary.false_branch, names, count, max_names);
        return count;
    default:
        return count;
    }
}

/**
 * @brief Collect all identifier and field references from AST (for codegen).
 * @param ast AST root to traverse.
 * @param names Output array to fill with unique reference names.
 * @param max_names Maximum capacity of names array.
 * @return Number of unique references collected.
 */
size_t cxpr_ast_references(const cxpr_ast* ast, const char** names, size_t max_names) {
    return cxpr_collect_references(ast, names, 0, max_names);
}

/**
 * @brief Recursive helper collecting function call names from AST.
 * @param ast AST subtree to traverse.
 * @param names Output array of unique function names.
 * @param count Current number of names collected.
 * @param max_names Maximum capacity of names array.
 * @return Updated count after collection.
 */
static size_t cxpr_collect_functions(const cxpr_ast* ast, const char** names,
                                    size_t count, size_t max_names) {
    if (!ast) return count;

    switch (ast->type) {
    case CXPR_NODE_FUNCTION_CALL:
        count = cxpr_add_unique_name(names, count, max_names, ast->data.function_call.name);
        /* Also recurse into arguments (which may contain nested function calls) */
        for (size_t i = 0; i < ast->data.function_call.argc; i++) {
            count = cxpr_collect_functions(ast->data.function_call.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0; i < ast->data.producer_access.argc; i++) {
            count = cxpr_collect_functions(ast->data.producer_access.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_BINARY_OP:
        count = cxpr_collect_functions(ast->data.binary_op.left, names, count, max_names);
        count = cxpr_collect_functions(ast->data.binary_op.right, names, count, max_names);
        return count;
    case CXPR_NODE_UNARY_OP:
        return cxpr_collect_functions(ast->data.unary_op.operand, names, count, max_names);
    case CXPR_NODE_TERNARY:
        count = cxpr_collect_functions(ast->data.ternary.condition, names, count, max_names);
        count = cxpr_collect_functions(ast->data.ternary.true_branch, names, count, max_names);
        count = cxpr_collect_functions(ast->data.ternary.false_branch, names, count, max_names);
        return count;
    default:
        return count;
    }
}

/**
 * @brief Collect all function names called in the AST.
 * @param ast AST root to traverse.
 * @param names Output array to fill with unique function names.
 * @param max_names Maximum capacity of names array.
 * @return Number of unique function names collected.
 */
size_t cxpr_ast_functions_used(const cxpr_ast* ast, const char** names, size_t max_names) {
    return cxpr_collect_functions(ast, names, 0, max_names);
}

/**
 * @brief Recursive helper collecting $variable names from AST.
 * @param ast AST subtree to traverse.
 * @param names Output array of unique variable names.
 * @param count Current number of names collected.
 * @param max_names Maximum capacity of names array.
 * @return Updated count after collection.
 */
static size_t cxpr_collect_variables(const cxpr_ast* ast, const char** names,
                                    size_t count, size_t max_names) {
    if (!ast) return count;

    switch (ast->type) {
    case CXPR_NODE_VARIABLE:
        return cxpr_add_unique_name(names, count, max_names, ast->data.variable.name);
    case CXPR_NODE_BINARY_OP:
        count = cxpr_collect_variables(ast->data.binary_op.left, names, count, max_names);
        count = cxpr_collect_variables(ast->data.binary_op.right, names, count, max_names);
        return count;
    case CXPR_NODE_UNARY_OP:
        return cxpr_collect_variables(ast->data.unary_op.operand, names, count, max_names);
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0; i < ast->data.function_call.argc; i++) {
            count = cxpr_collect_variables(ast->data.function_call.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_PRODUCER_ACCESS:
        for (size_t i = 0; i < ast->data.producer_access.argc; i++) {
            count = cxpr_collect_variables(ast->data.producer_access.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_TERNARY:
        count = cxpr_collect_variables(ast->data.ternary.condition, names, count, max_names);
        count = cxpr_collect_variables(ast->data.ternary.true_branch, names, count, max_names);
        count = cxpr_collect_variables(ast->data.ternary.false_branch, names, count, max_names);
        return count;
    default:
        return count;
    }
}

/**
 * @brief Collect all $variable names used in the AST.
 * @param ast AST root to traverse.
 * @param names Output array to fill with unique variable names.
 * @param max_names Maximum capacity of names array.
 * @return Number of unique variable names collected.
 */
size_t cxpr_ast_variables_used(const cxpr_ast* ast, const char** names, size_t max_names) {
    return cxpr_collect_variables(ast, names, 0, max_names);
}
