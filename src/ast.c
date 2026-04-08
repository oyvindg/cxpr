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

    {
        char* p = node->data.chain_access.full_key;
        for (size_t i = 0; i < depth; i++) {
            if (i > 0) *p++ = '.';
            size_t len = strlen(path[i]);
            memcpy(p, path[i], len);
            p += len;
        }
        *p = '\0';
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

cxpr_ast* cxpr_ast_new_lookback(cxpr_ast* target, cxpr_ast* index) {
    cxpr_ast* node = (cxpr_ast*)calloc(1, sizeof(cxpr_ast));
    if (!node) return NULL;
    node->type = CXPR_NODE_LOOKBACK;
    node->data.lookback.target = target;
    node->data.lookback.index = index;
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
        for (size_t i = 0; i < ast->data.function_call.argc; i++) {
            cxpr_ast_free(ast->data.function_call.args[i]);
        }
        free(ast->data.function_call.args);
        break;
    case CXPR_NODE_LOOKBACK:
        cxpr_ast_free(ast->data.lookback.target);
        cxpr_ast_free(ast->data.lookback.index);
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
const cxpr_ast* cxpr_ast_lookback_target(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_LOOKBACK) ? ast->data.lookback.target : NULL;
}

const cxpr_ast* cxpr_ast_lookback_index(const cxpr_ast* ast) {
    return (ast && ast->type == CXPR_NODE_LOOKBACK) ? ast->data.lookback.index : NULL;
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
    case CXPR_NODE_LOOKBACK:
        count = cxpr_collect_references(ast->data.lookback.target, names, count, max_names);
        count = cxpr_collect_references(ast->data.lookback.index, names, count, max_names);
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
    case CXPR_NODE_LOOKBACK:
        count = cxpr_collect_functions(ast->data.lookback.target, names, count, max_names);
        count = cxpr_collect_functions(ast->data.lookback.index, names, count, max_names);
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
    case CXPR_NODE_LOOKBACK:
        count = cxpr_collect_variables(ast->data.lookback.target, names, count, max_names);
        count = cxpr_collect_variables(ast->data.lookback.index, names, count, max_names);
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

static size_t cxpr_collect_field_paths(const cxpr_ast* ast, const char** names,
                                       size_t count, size_t max_names) {
    if (!ast) return count;

    switch (ast->type) {
    case CXPR_NODE_FIELD_ACCESS:
        return cxpr_add_unique_name(names, count, max_names, ast->data.field_access.full_key);
    case CXPR_NODE_CHAIN_ACCESS:
        return cxpr_add_unique_name(names, count, max_names, ast->data.chain_access.full_key);
    case CXPR_NODE_PRODUCER_ACCESS:
        count = cxpr_add_unique_name(names, count, max_names, ast->data.producer_access.full_key);
        for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
            count = cxpr_collect_field_paths(ast->data.producer_access.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_FUNCTION_CALL:
        for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
            count = cxpr_collect_field_paths(ast->data.function_call.args[i], names, count, max_names);
        }
        return count;
    case CXPR_NODE_LOOKBACK:
        count = cxpr_collect_field_paths(ast->data.lookback.target, names, count, max_names);
        count = cxpr_collect_field_paths(ast->data.lookback.index, names, count, max_names);
        return count;
    case CXPR_NODE_BINARY_OP:
        count = cxpr_collect_field_paths(ast->data.binary_op.left, names, count, max_names);
        count = cxpr_collect_field_paths(ast->data.binary_op.right, names, count, max_names);
        return count;
    case CXPR_NODE_UNARY_OP:
        return cxpr_collect_field_paths(ast->data.unary_op.operand, names, count, max_names);
    case CXPR_NODE_TERNARY:
        count = cxpr_collect_field_paths(ast->data.ternary.condition, names, count, max_names);
        count = cxpr_collect_field_paths(ast->data.ternary.true_branch, names, count, max_names);
        count = cxpr_collect_field_paths(ast->data.ternary.false_branch, names, count, max_names);
        return count;
    default:
        return count;
    }
}

static void cxpr_analysis_set_error(cxpr_error* err, cxpr_error_code code, const char* message) {
    if (!err) return;
    *err = (cxpr_error){0};
    err->code = code;
    err->message = message;
}

static cxpr_expr_type cxpr_analysis_merge_types(cxpr_expr_type a, cxpr_expr_type b) {
    if (a == b) return a;
    if (a == CXPR_EXPR_UNKNOWN) return b;
    if (b == CXPR_EXPR_UNKNOWN) return a;
    return CXPR_EXPR_UNKNOWN;
}

static void cxpr_analysis_merge_into(cxpr_analysis* dst, const cxpr_analysis* src) {
    if (!dst || !src) return;
    dst->uses_variables = dst->uses_variables || src->uses_variables;
    dst->uses_parameters = dst->uses_parameters || src->uses_parameters;
    dst->uses_functions = dst->uses_functions || src->uses_functions;
    dst->uses_expressions = dst->uses_expressions || src->uses_expressions;
    dst->uses_field_access = dst->uses_field_access || src->uses_field_access;
    dst->can_short_circuit = dst->can_short_circuit || src->can_short_circuit;
    dst->is_constant = dst->is_constant && src->is_constant;
    dst->node_count += src->node_count;
    if (src->max_depth > dst->max_depth) dst->max_depth = src->max_depth;
}

static bool cxpr_analysis_validate_arity(const cxpr_func_entry* entry, size_t argc, cxpr_error* err) {
    if (!entry) return false;
    if (argc < entry->min_args || argc > entry->max_args) {
        cxpr_analysis_set_error(err, CXPR_ERR_WRONG_ARITY, "Wrong number of arguments");
        return false;
    }
    return true;
}

static bool cxpr_analyze_node(const cxpr_ast* ast, const cxpr_registry* reg,
                              cxpr_analysis* out, unsigned depth, cxpr_error* err) {
    cxpr_func_entry* entry;

    if (!ast) return true;

    out->node_count += 1;
    if (depth > out->max_depth) out->max_depth = depth;

    switch (ast->type) {
    case CXPR_NODE_NUMBER:
        out->result_type = CXPR_EXPR_NUMBER;
        return true;

    case CXPR_NODE_BOOL:
        out->result_type = CXPR_EXPR_BOOL;
        out->is_predicate = true;
        return true;

    case CXPR_NODE_IDENTIFIER:
        out->uses_variables = true;
        out->is_constant = false;
        out->result_type = CXPR_EXPR_UNKNOWN;
        return true;

    case CXPR_NODE_VARIABLE:
        out->uses_parameters = true;
        out->is_constant = false;
        out->result_type = CXPR_EXPR_UNKNOWN;
        return true;

    case CXPR_NODE_FIELD_ACCESS:
    case CXPR_NODE_CHAIN_ACCESS:
        out->uses_variables = true;
        out->uses_field_access = true;
        out->is_constant = false;
        out->result_type = CXPR_EXPR_UNKNOWN;
        return true;

    case CXPR_NODE_UNARY_OP: {
        if (!cxpr_analyze_node(ast->data.unary_op.operand, reg, out, depth + 1, err)) return false;
        out->result_type =
            (ast->data.unary_op.op == CXPR_TOK_NOT) ? CXPR_EXPR_BOOL : CXPR_EXPR_NUMBER;
        out->is_predicate = (out->result_type == CXPR_EXPR_BOOL);
        return true;
    }

    case CXPR_NODE_LOOKBACK: {
        cxpr_analysis target_analysis;
        cxpr_analysis index_analysis;
        memset(&target_analysis, 0, sizeof(target_analysis));
        memset(&index_analysis, 0, sizeof(index_analysis));
        target_analysis.is_constant = true;
        index_analysis.is_constant = true;
        if (!cxpr_analyze_node(ast->data.lookback.target, reg, &target_analysis, depth + 1, err)) {
            return false;
        }
        if (!cxpr_analyze_node(ast->data.lookback.index, reg, &index_analysis, depth + 1, err)) {
            return false;
        }
        cxpr_analysis_merge_into(out, &target_analysis);
        cxpr_analysis_merge_into(out, &index_analysis);
        out->result_type = target_analysis.result_type;
        out->is_predicate = target_analysis.is_predicate;
        out->is_constant = false;
        return true;
    }

    case CXPR_NODE_BINARY_OP: {
        if (!cxpr_analyze_node(ast->data.binary_op.left, reg, out, depth + 1, err)) return false;
        if (!cxpr_analyze_node(ast->data.binary_op.right, reg, out, depth + 1, err)) return false;

        switch (ast->data.binary_op.op) {
        case CXPR_TOK_AND:
        case CXPR_TOK_OR:
            out->result_type = CXPR_EXPR_BOOL;
            out->is_predicate = true;
            out->can_short_circuit = true;
            return true;
        case CXPR_TOK_EQ:
        case CXPR_TOK_NEQ:
        case CXPR_TOK_LT:
        case CXPR_TOK_GT:
        case CXPR_TOK_LTE:
        case CXPR_TOK_GTE:
            out->result_type = CXPR_EXPR_BOOL;
            out->is_predicate = true;
            return true;
        default:
            out->result_type = CXPR_EXPR_NUMBER;
            out->is_predicate = false;
            return true;
        }
    }

    case CXPR_NODE_FUNCTION_CALL:
        out->uses_functions = true;
        out->is_constant = false;
        if (strcmp(ast->data.function_call.name, "if") == 0 &&
            ast->data.function_call.argc == 3) {
            cxpr_analysis branch_true;
            cxpr_analysis branch_false;
            if (!cxpr_analyze_node(ast->data.function_call.args[0], reg, out, depth + 1, err)) {
                return false;
            }
            memset(&branch_true, 0, sizeof(branch_true));
            memset(&branch_false, 0, sizeof(branch_false));
            branch_true.is_constant = true;
            branch_false.is_constant = true;
            if (!cxpr_analyze_node(ast->data.function_call.args[1], reg, &branch_true, depth + 1, err)) {
                return false;
            }
            if (!cxpr_analyze_node(ast->data.function_call.args[2], reg, &branch_false, depth + 1, err)) {
                return false;
            }
            cxpr_analysis_merge_into(out, &branch_true);
            cxpr_analysis_merge_into(out, &branch_false);
            out->can_short_circuit = true;
            out->result_type = cxpr_analysis_merge_types(branch_true.result_type, branch_false.result_type);
            out->is_predicate = (out->result_type == CXPR_EXPR_BOOL);
            return true;
        }
        entry = reg ? cxpr_registry_find(reg, ast->data.function_call.name) : NULL;
        if (reg && !entry) {
            out->has_unknown_functions = true;
            out->first_unknown_function = ast->data.function_call.name;
            cxpr_analysis_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION, "Unknown function");
            return false;
        }
        if (entry) {
            if (!cxpr_analysis_validate_arity(entry, ast->data.function_call.argc, err)) return false;
            if (entry->defined_body) out->uses_expressions = true;
        }
        for (size_t i = 0; i < ast->data.function_call.argc; ++i) {
            if (!cxpr_analyze_node(ast->data.function_call.args[i], reg, out, depth + 1, err)) {
                return false;
            }
        }
        if (entry) {
            if (entry->struct_producer && !entry->sync_func && !entry->value_func && !entry->typed_func) {
                out->result_type = CXPR_EXPR_STRUCT;
            } else if (entry->has_return_type) {
                switch (entry->return_type) {
                case CXPR_VALUE_BOOL: out->result_type = CXPR_EXPR_BOOL; break;
                case CXPR_VALUE_STRUCT: out->result_type = CXPR_EXPR_STRUCT; break;
                default: out->result_type = CXPR_EXPR_NUMBER; break;
                }
            } else if (entry->sync_func && !entry->value_func && !entry->typed_func) {
                out->result_type = CXPR_EXPR_NUMBER;
            } else {
                out->result_type = CXPR_EXPR_UNKNOWN;
            }
        } else {
            out->result_type = CXPR_EXPR_UNKNOWN;
        }
        out->is_predicate = (out->result_type == CXPR_EXPR_BOOL);
        return true;

    case CXPR_NODE_PRODUCER_ACCESS:
        out->uses_functions = true;
        out->uses_field_access = true;
        out->is_constant = false;
        entry = reg ? cxpr_registry_find(reg, ast->data.producer_access.name) : NULL;
        if (reg && !entry) {
            out->has_unknown_functions = true;
            out->first_unknown_function = ast->data.producer_access.name;
            cxpr_analysis_set_error(err, CXPR_ERR_UNKNOWN_FUNCTION, "Unknown function");
            return false;
        }
        if (entry) {
            if (!cxpr_analysis_validate_arity(entry, ast->data.producer_access.argc, err)) return false;
        }
        for (size_t i = 0; i < ast->data.producer_access.argc; ++i) {
            if (!cxpr_analyze_node(ast->data.producer_access.args[i], reg, out, depth + 1, err)) {
                return false;
            }
        }
        out->result_type = CXPR_EXPR_UNKNOWN;
        out->is_predicate = false;
        return true;

    case CXPR_NODE_TERNARY: {
        if (!cxpr_analyze_node(ast->data.ternary.condition, reg, out, depth + 1, err)) return false;
        cxpr_analysis branch_true;
        cxpr_analysis branch_false;

        memset(&branch_true, 0, sizeof(branch_true));
        memset(&branch_false, 0, sizeof(branch_false));
        branch_true.is_constant = true;
        branch_false.is_constant = true;

        if (!cxpr_analyze_node(ast->data.ternary.true_branch, reg, &branch_true, depth + 1, err)) {
            return false;
        }
        if (!cxpr_analyze_node(ast->data.ternary.false_branch, reg, &branch_false, depth + 1, err)) {
            return false;
        }

        cxpr_analysis_merge_into(out, &branch_true);
        cxpr_analysis_merge_into(out, &branch_false);
        out->can_short_circuit = true;
        out->result_type = cxpr_analysis_merge_types(branch_true.result_type, branch_false.result_type);
        out->is_predicate = (out->result_type == CXPR_EXPR_BOOL);
        return true;
    }

    default:
        out->result_type = CXPR_EXPR_UNKNOWN;
        return true;
    }
}

bool cxpr_analyze(const cxpr_ast* ast, const cxpr_registry* reg,
                  cxpr_analysis* out_analysis, cxpr_error* err) {
    const char* names[256];

    if (err) *err = (cxpr_error){0};
    if (!ast || !out_analysis) return false;

    memset(out_analysis, 0, sizeof(*out_analysis));
    out_analysis->is_constant = true;

    if (!cxpr_analyze_node(ast, reg, out_analysis, 1u, err)) return false;

    out_analysis->reference_count = cxpr_ast_references(ast, names, 256);
    out_analysis->function_count = cxpr_ast_functions_used(ast, names, 256);
    out_analysis->parameter_count = cxpr_ast_variables_used(ast, names, 256);
    out_analysis->field_path_count = cxpr_collect_field_paths(ast, names, 0, 256);
    return true;
}

bool cxpr_analyze_expr(const char* expression, const cxpr_registry* reg,
                       cxpr_analysis* out_analysis, cxpr_error* err) {
    cxpr_parser* parser;
    cxpr_ast* ast;
    bool ok;

    if (err) *err = (cxpr_error){0};
    if (!expression || !out_analysis) {
        cxpr_analysis_set_error(err, CXPR_ERR_SYNTAX, "NULL argument");
        return false;
    }

    parser = cxpr_parser_new();
    if (!parser) {
        cxpr_analysis_set_error(err, CXPR_ERR_OUT_OF_MEMORY, "Out of memory");
        return false;
    }

    ast = cxpr_parse(parser, expression, err);
    if (!ast) {
        cxpr_parser_free(parser);
        return false;
    }

    ok = cxpr_analyze(ast, reg, out_analysis, err);
    cxpr_ast_free(ast);
    cxpr_parser_free(parser);
    return ok;
}
