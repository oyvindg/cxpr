/**
 * @file internal.h
 * @brief Internal structures and declarations for cxpr.
 *
 * This file defines the concrete types behind the opaque handles
 * declared in cxpr.h. It is NOT part of the public API.
 */

#ifndef CXPR_INTERNAL_H
#define CXPR_INTERNAL_H

#include <cxpr/cxpr.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Duplicate a NUL-terminated string using cxpr's internal allocator path.
 * @param s Source string to copy.
 * @return Newly allocated copy of `s`, or NULL if `s` is NULL or allocation fails.
 */
static inline char* cxpr_strdup(const char* s) {
    size_t len;
    char* copy;

    if (!s) return NULL;
    len = strlen(s) + 1;
    copy = (char*)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    return copy;
}

/**
 * @brief Reentrant tokenization helper compatible with cxpr's ISO C build.
 * @param str String to tokenize on the first call, or NULL to continue tokenizing.
 * @param delim Set of delimiter characters.
 * @param saveptr In/out cursor storing tokenizer state between calls.
 * @return Pointer to the next token within `str`, or NULL when no tokens remain
 *         or input arguments are invalid.
 */
static inline char* cxpr_strtok_r(char* str, const char* delim, char** saveptr) {
    char* start;
    char* end;

    if (!delim || !saveptr) return NULL;

    start = str ? str : *saveptr;
    if (!start) return NULL;

    start += strspn(start, delim);
    if (*start == '\0') {
        *saveptr = start;
        return NULL;
    }

    end = start + strcspn(start, delim);
    if (*end == '\0') {
        *saveptr = end;
    } else {
        *end = '\0';
        *saveptr = end + 1;
    }
    return start;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Token types (used by lexer and parser)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    /* Literals */
    CXPR_TOK_NUMBER,            /**< Numeric literal */
    CXPR_TOK_IDENTIFIER,        /**< Identifier (e.g. "rsi", "ema_fast") */
    CXPR_TOK_VARIABLE,          /**< Parameter variable ($name) */
    CXPR_TOK_TRUE,              /**< true */
    CXPR_TOK_FALSE,             /**< false */
    CXPR_TOK_STRING,            /**< String literal (reserved for future) */

    /* Arithmetic operators */
    CXPR_TOK_PLUS,              /**< + */
    CXPR_TOK_MINUS,             /**< - */
    CXPR_TOK_STAR,              /**< * */
    CXPR_TOK_SLASH,             /**< / */
    CXPR_TOK_PERCENT,           /**< % */
    CXPR_TOK_POWER,             /**< ^ or ** */

    /* Comparison operators */
    CXPR_TOK_EQ,                /**< == */
    CXPR_TOK_NEQ,               /**< != */
    CXPR_TOK_LT,                /**< < */
    CXPR_TOK_GT,                /**< > */
    CXPR_TOK_LTE,               /**< <= */
    CXPR_TOK_GTE,               /**< >= */

    /* Logical operators */
    CXPR_TOK_AND,               /**< && or and */
    CXPR_TOK_OR,                /**< || or or */
    CXPR_TOK_NOT,               /**< ! or not */

    /* Delimiters */
    CXPR_TOK_LPAREN,            /**< ( */
    CXPR_TOK_RPAREN,            /**< ) */
    CXPR_TOK_COMMA,             /**< , */
    CXPR_TOK_DOT,               /**< . */
    CXPR_TOK_QUESTION,          /**< ? (ternary) */
    CXPR_TOK_COLON,             /**< : (ternary) */

    /* Special */
    CXPR_TOK_EOF,               /**< End of input */
    CXPR_TOK_ERROR              /**< Lexer error */
} cxpr_token_type;

/* ═══════════════════════════════════════════════════════════════════════════
 * Token structure
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief A single token produced by the lexer.
 */
typedef struct {
    cxpr_token_type type;       /**< Token type */
    const char* start;        /**< Pointer to start in source string */
    size_t length;            /**< Length of token text */
    double number_value;      /**< Numeric value (valid for CXPR_TOK_NUMBER) */
    size_t position;          /**< Absolute byte position in source */
    size_t line;              /**< Line number (1-based) */
    size_t column;            /**< Column number (1-based) */
} cxpr_token;

/* ═══════════════════════════════════════════════════════════════════════════
 * Lexer structure
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Lexer state for tokenizing an expression string.
 */
typedef struct {
    const char* source;       /**< Source expression string */
    const char* current;      /**< Current position in source */
    size_t line;              /**< Current line number (1-based) */
    size_t column;            /**< Current column number (1-based) */
    size_t position;          /**< Current absolute byte position */
} cxpr_lexer;

/** @brief Initialize lexer state for a source string. */
void cxpr_lexer_init(cxpr_lexer* lexer, const char* source);
/** @brief Consume and return the next token. */
cxpr_token cxpr_lexer_next(cxpr_lexer* lexer);
/** @brief Peek at the next token without consuming it. */
cxpr_token cxpr_lexer_peek(cxpr_lexer* lexer);

/* ═══════════════════════════════════════════════════════════════════════════
 * AST node structure (tagged union)
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief AST node — the public cxpr_ast handle points to this.
 *
 * Uses a tagged union since C11 has no inheritance.
 * Each node owns its children and strings (via strdup).
 */
struct cxpr_ast {
    cxpr_node_type type;

    union {
        /* CXPR_NODE_NUMBER */
        struct {
            double value;
        } number;

        /* CXPR_NODE_BOOL */
        struct {
            bool value;
        } boolean;

        /* CXPR_NODE_IDENTIFIER */
        struct {
            char* name;               /**< Owned, free with free() */
        } identifier;

        /* CXPR_NODE_VARIABLE */
        struct {
            char* name;               /**< Owned, without $ prefix */
        } variable;

        /* CXPR_NODE_FIELD_ACCESS */
        struct {
            char* object;             /**< e.g. "macd" */
            char* field;              /**< e.g. "histogram" */
            char* full_key;           /**< e.g. "macd.histogram" (for context lookup) */
        } field_access;

        /* CXPR_NODE_CHAIN_ACCESS */
        struct {
            char** path;
            size_t depth;
            char* full_key;
        } chain_access;

        /* CXPR_NODE_PRODUCER_ACCESS */
        struct {
            char* name;
            struct cxpr_ast** args;
            size_t argc;
            char* field;
            char* full_key;
            const struct cxpr_registry* cached_registry;
            unsigned long cached_registry_version;
            size_t cached_entry_index;
            bool cached_entry_found;
            bool cached_lookup_valid;
            char* cached_const_key;
            bool cached_const_key_ready;
            size_t cached_field_index;
            bool cached_field_index_valid;
        } producer_access;

        /* CXPR_NODE_BINARY_OP */
        struct {
            int op;                   /**< cxpr_token_type of the operator */
            struct cxpr_ast* left;
            struct cxpr_ast* right;
        } binary_op;

        /* CXPR_NODE_UNARY_OP */
        struct {
            int op;                   /**< cxpr_token_type of the operator */
            struct cxpr_ast* operand;
        } unary_op;

        /* CXPR_NODE_FUNCTION_CALL */
        struct {
            char* name;               /**< Function name, owned */
            struct cxpr_ast** args;     /**< Array of argument ASTs, owned */
            size_t argc;              /**< Number of arguments */
            const struct cxpr_registry* cached_registry; /**< Registry used for cached entry */
            unsigned long cached_registry_version;       /**< Registry version of cached entry */
            size_t cached_entry_index;                   /**< Cached entry index inside registry */
            bool cached_entry_found;                     /**< Whether cached index resolved to a function */
            bool cached_lookup_valid;                    /**< True when cache fields are initialized */
        } function_call;

        /* CXPR_NODE_TERNARY */
        struct {
            struct cxpr_ast* condition;
            struct cxpr_ast* true_branch;
            struct cxpr_ast* false_branch;
        } ternary;
    } data;
};

/** @name AST construction helpers (used by parser)
 * @{ */
/** @brief Create a NUMBER literal node. */
cxpr_ast* cxpr_ast_new_number(double value);
cxpr_ast* cxpr_ast_new_bool(bool value);
/** @brief Create an IDENTIFIER node (strdup's name). */
cxpr_ast* cxpr_ast_new_identifier(const char* name);
/** @brief Create a VARIABLE ($param) node (strdup's name). */
cxpr_ast* cxpr_ast_new_variable(const char* name);
/** @brief Create a FIELD_ACCESS node (e.g. macd.histogram). */
cxpr_ast* cxpr_ast_new_field_access(const char* object, const char* field);
/** @brief Create a CHAIN_ACCESS node (e.g. outer.inner.value). */
cxpr_ast* cxpr_ast_new_chain_access(const char* const* path, size_t depth);
/** @brief Create a PRODUCER_ACCESS node (e.g. macd(12, 3).line). */
cxpr_ast* cxpr_ast_new_producer_access(const char* name, cxpr_ast** args, size_t argc,
                                       const char* field);
/** @brief Create a BINARY_OP node (op + left/right children). */
cxpr_ast* cxpr_ast_new_binary_op(int op, cxpr_ast* left, cxpr_ast* right);
/** @brief Create a UNARY_OP node (op + operand child). */
cxpr_ast* cxpr_ast_new_unary_op(int op, cxpr_ast* operand);
/** @brief Create a FUNCTION_CALL node (takes ownership of args). */
cxpr_ast* cxpr_ast_new_function_call(const char* name, cxpr_ast** args, size_t argc);
/** @brief Create a TERNARY conditional node. */
cxpr_ast* cxpr_ast_new_ternary(cxpr_ast* condition, cxpr_ast* true_branch, cxpr_ast* false_branch);
/** @} */

/* ═══════════════════════════════════════════════════════════════════════════
 * Hash map (internal, used by context and registry)
 * ═══════════════════════════════════════════════════════════════════════════ */

#define CXPR_HASHMAP_INITIAL_CAPACITY 32
#define CXPR_HASHMAP_LOAD_FACTOR 0.75
#define CXPR_CONTEXT_ENTRY_CACHE_SIZE 64

/**
 * @brief Hash map entry for string → double.
 */
typedef struct {
    char* key;                /**< Owned, NULL = empty slot */
    double value;
} cxpr_hashmap_entry;

/**
 * @brief Open-addressing hash map (string keys, double values).
 */
typedef struct {
    cxpr_hashmap_entry* entries;
    size_t capacity;
    size_t count;
} cxpr_hashmap;

/** @brief Find the backing hash-map slot for a prehashed key, or NULL if absent. */
const cxpr_hashmap_entry* cxpr_hashmap_find_prehashed_entry(const cxpr_hashmap* map,
                                                            const char* key,
                                                            unsigned long hash);
/** @brief Find the mutable backing slot for a prehashed key, or NULL if absent. */
cxpr_hashmap_entry* cxpr_hashmap_find_prehashed_slot(cxpr_hashmap* map, const char* key,
                                                     unsigned long hash);

/** @brief Initialize a hash map with default capacity. */
void cxpr_hashmap_init(cxpr_hashmap* map);
/** @brief Free all entries and the map's storage. */
void cxpr_hashmap_destroy(cxpr_hashmap* map);
/** @brief Insert or update a key-value pair; returns true when key layout changed. */
bool cxpr_hashmap_set(cxpr_hashmap* map, const char* key, double value);
/** @brief Insert or update a key-value pair using a caller-precomputed hash. */
bool cxpr_hashmap_set_prehashed(cxpr_hashmap* map, const char* key,
                                unsigned long hash, double value);
/** @brief Look up a value by key. */
double cxpr_hashmap_get(const cxpr_hashmap* map, const char* key, bool* found);
/** @brief Precompute the internal string hash used by cxpr hash maps. */
unsigned long cxpr_hash_string(const char* str);
/** @brief Look up a value by key using a precomputed hash. */
double cxpr_hashmap_get_prehashed(const cxpr_hashmap* map, const char* key,
                                  unsigned long hash, bool* found);
/** @brief Remove all entries without freeing the map itself. */
void cxpr_hashmap_clear(cxpr_hashmap* map);
/** @brief Deep copy a hash map. */
cxpr_hashmap* cxpr_hashmap_clone(const cxpr_hashmap* map);

typedef struct {
    char* name;
    cxpr_struct_value* value;
} cxpr_struct_map_entry;

typedef struct {
    cxpr_struct_map_entry* entries;
    size_t capacity;
    size_t count;
} cxpr_struct_map;

void cxpr_struct_map_init(cxpr_struct_map* map);
void cxpr_struct_map_destroy(cxpr_struct_map* map);
void cxpr_struct_map_clear(cxpr_struct_map* map);
bool cxpr_struct_map_clone(cxpr_struct_map* dst, const cxpr_struct_map* src);
void cxpr_context_store_struct(cxpr_struct_map* map, const char* name,
                               const cxpr_struct_value* value);
const cxpr_struct_value* cxpr_context_lookup_struct_map(const cxpr_struct_map* map,
                                                        const char* name);
void cxpr_context_set_cached_struct(cxpr_context* ctx, const char* name,
                                    const cxpr_struct_value* value);
const cxpr_struct_value* cxpr_context_get_cached_struct(const cxpr_context* ctx,
                                                        const char* name);

typedef struct {
    const char*         key_ref;
    unsigned long       hash;
    size_t              slot;
    cxpr_hashmap_entry* entries_base;
} cxpr_context_entry_cache;

/* ═══════════════════════════════════════════════════════════════════════════
 * Context structure
 * ═══════════════════════════════════════════════════════════════════════════ */

struct cxpr_context {
    cxpr_hashmap variables;     /**< Runtime variables */
    cxpr_hashmap params;        /**< Compile-time parameters ($name) */
    cxpr_struct_map structs;    /**< Native typed structs */
    cxpr_struct_map cached_structs; /**< Cached producer outputs */
    cxpr_context_entry_cache variable_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache param_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache variable_ptr_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    cxpr_context_entry_cache param_ptr_cache[CXPR_CONTEXT_ENTRY_CACHE_SIZE];
    unsigned long variables_version; /**< Bumped on variable-map structural changes */
    unsigned long params_version;    /**< Bumped on param-map structural changes */
    const struct cxpr_context* parent; /**< Optional parent context for local overlays */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Registry structures
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief A single registered function entry.
 */
typedef struct {
    char* name;               /**< Function name, owned */
    cxpr_func_ptr sync_func;    /**< Sync function pointer (or NULL for defined functions) */
    cxpr_struct_producer_ptr struct_producer; /**< Struct-producing callback */
    enum {
        CXPR_NATIVE_KIND_NONE = 0,
        CXPR_NATIVE_KIND_NULLARY,
        CXPR_NATIVE_KIND_UNARY,
        CXPR_NATIVE_KIND_BINARY,
        CXPR_NATIVE_KIND_TERNARY
    } native_kind;
    union {
        double (*nullary)(void);
        double (*unary)(double);
        double (*binary)(double, double);
        double (*ternary)(double, double, double);
    } native_scalar;
    size_t min_args;
    size_t max_args;
    void* userdata;
    cxpr_userdata_free_fn userdata_free;
    /* Struct expansion (NULL when not struct-aware) */
    char** struct_fields;     /**< Owned array of field name strings (e.g. {"x","y","z"}) */
    size_t fields_per_arg;    /**< Number of fields per struct argument */
    size_t struct_argc;       /**< Number of struct arguments accepted */
    /* Defined function (expression-based, via cxpr_registry_define_fn) */
    cxpr_ast*  defined_body;               /**< Parsed body AST; NULL for C functions */
    cxpr_program* defined_program;         /**< Lazily compiled body program; NULL until needed */
    bool       defined_program_failed;     /**< Sticky flag when program compilation is unsupported */
    char**     defined_param_names;        /**< Parameter name array, owned */
    size_t     defined_param_count;        /**< Number of parameters */
    char***    defined_param_fields;       /**< Per-param field lists; NULL entry = scalar */
    size_t*    defined_param_field_counts; /**< Per-param field counts */
} cxpr_func_entry;

#define CXPR_REGISTRY_INITIAL_CAPACITY 64

struct cxpr_registry {
    cxpr_func_entry* entries;   /**< Array of function entries */
    size_t capacity;
    size_t count;
    unsigned long version;      /**< Bumped whenever entries mutate or move */
};

/** @brief Find a function entry by name in the registry. */
cxpr_func_entry* cxpr_registry_find(const cxpr_registry* reg, const char* name);

/* ═══════════════════════════════════════════════════════════════════════════
 * Parser structure
 * ═══════════════════════════════════════════════════════════════════════════ */

struct cxpr_parser {
    cxpr_lexer lexer;
    cxpr_token current;         /**< Current token */
    cxpr_token previous;        /**< Previous token (for error reporting) */
    bool had_error;
    cxpr_error last_error;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal IR / compiled plan structures
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Minimal opcode set for IR v1.
 *
 * V1 intentionally supports only constants, runtime variables, $params,
 * basic arithmetic, and return.
 */
typedef enum {
    CXPR_OP_PUSH_CONST,
    CXPR_OP_PUSH_BOOL,
    CXPR_OP_LOAD_LOCAL,
    CXPR_OP_LOAD_LOCAL_SQUARE,
    CXPR_OP_LOAD_VAR,
    CXPR_OP_LOAD_VAR_SQUARE,
    CXPR_OP_LOAD_PARAM,
    CXPR_OP_LOAD_PARAM_SQUARE,
    CXPR_OP_LOAD_FIELD,
    CXPR_OP_LOAD_FIELD_SQUARE,
    CXPR_OP_LOAD_CHAIN,
    CXPR_OP_ADD,
    CXPR_OP_SUB,
    CXPR_OP_MUL,
    CXPR_OP_SQUARE,
    CXPR_OP_DIV,
    CXPR_OP_MOD,
    CXPR_OP_CMP_EQ,
    CXPR_OP_CMP_NEQ,
    CXPR_OP_CMP_LT,
    CXPR_OP_CMP_LTE,
    CXPR_OP_CMP_GT,
    CXPR_OP_CMP_GTE,
    CXPR_OP_NOT,
    CXPR_OP_NEG,
    CXPR_OP_SIGN,
    CXPR_OP_SQRT,
    CXPR_OP_ABS,
    CXPR_OP_FLOOR,
    CXPR_OP_CEIL,
    CXPR_OP_ROUND,
    CXPR_OP_POW,
    CXPR_OP_CLAMP,
    CXPR_OP_CALL_PRODUCER,
    CXPR_OP_CALL_PRODUCER_CONST,
    CXPR_OP_CALL_PRODUCER_CONST_FIELD,
    CXPR_OP_GET_FIELD,
    CXPR_OP_CALL_UNARY,
    CXPR_OP_CALL_BINARY,
    CXPR_OP_CALL_TERNARY,
    CXPR_OP_CALL_FUNC,
    CXPR_OP_CALL_DEFINED,
    CXPR_OP_CALL_AST,
    CXPR_OP_JUMP,
    CXPR_OP_JUMP_IF_FALSE,
    CXPR_OP_JUMP_IF_TRUE,
    CXPR_OP_RETURN
} cxpr_opcode;

/**
 * @brief A single IR instruction.
 *
 * The operand is interpreted according to opcode:
 * - PUSH_CONST: literal double in value
 * - LOAD_VAR / LOAD_PARAM / GET_FIELD: borrowed symbol name in name
 * - CALL_PRODUCER_CONST_FIELD: cache key in name, field name in aux_name
 * - CALL_PRODUCER_CONST_FIELD: payload points to owned constant double args
 * - CALL_AST: borrowed AST pointer in ast
 * - JUMP / conditional jumps: target index in index
 * - arithmetic / comparisons / return: no extra operand
 */
typedef struct {
    cxpr_opcode op;
    /* 4 bytes implicit padding */
    const char* name;
    const char* aux_name;
    const void* payload;
    const cxpr_func_entry* func;
    union {
        double value;        /* PUSH_CONST, PUSH_BOOL */
        unsigned long hash;  /* LOAD_VAR, LOAD_PARAM, LOAD_FIELD, LOAD_CHAIN */
        size_t index;        /* LOAD_LOCAL, CALL_*, CALL_DEFINED, CALL_PRODUCER, JUMP* */
        const cxpr_ast* ast; /* CALL_AST */
    };
} cxpr_ir_instr;

_Static_assert(sizeof(cxpr_ir_instr) <= 48, "cxpr_ir_instr for stor");

/**
 * @brief Cached LOAD_VAR / LOAD_PARAM resolution for one IR instruction.
 */
typedef struct {
    const cxpr_context* request_ctx;        /**< Context chain root used for the cached lookup */
    const cxpr_context* owner_ctx;          /**< Context owning the cached entry */
    cxpr_hashmap_entry* entries_base;       /**< Owner map base when the entry was cached */
    size_t              slot;               /**< Index into entries_base for the cached entry */
    unsigned long       shadow_version;     /**< Structural version summary below the owner */
} cxpr_ir_lookup_cache;

/**
 * @brief Internal compiled program representation.
 *
 * This stays internal until a public cxpr_program API is introduced.
 */
typedef struct {
    cxpr_ir_instr* code;        /**< Owned instruction array */
    size_t count;               /**< Number of instructions */
    size_t capacity;            /**< Allocated instruction capacity */
    const cxpr_ast* ast;        /**< Borrowed root AST for typed fallback evaluation */
    cxpr_ir_lookup_cache* lookup_cache; /**< Optional per-instruction lookup cache */
    unsigned char fast_result_kind; /**< 0=unknown, 1=double, 2=bool for scalar fast-path */
} cxpr_ir_program;

struct cxpr_program {
    cxpr_ir_program ir;
    const cxpr_ast* ast;
};

/** @brief Reset and free the storage owned by an IR program. */
void cxpr_ir_program_reset(cxpr_ir_program* program);
bool cxpr_ir_constant_value(const cxpr_ast* ast, double* out);
char* cxpr_ir_build_constant_producer_key(const char* name, const cxpr_ast* const* args,
                                          size_t argc);
/** @brief Compile a supported AST into an internal IR program. */
bool cxpr_ir_compile(const cxpr_ast* ast, const cxpr_registry* reg,
                     cxpr_ir_program* program, cxpr_error* err);
/** @brief Compile an AST into IR with local identifiers bound to slots. */
bool cxpr_ir_compile_with_locals(const cxpr_ast* ast, const cxpr_registry* reg,
                                 const char* const* local_names, size_t local_count,
                                 cxpr_ir_program* program, cxpr_error* err);
/** @brief Lazily compile a scalar-only defined function body to IR locals. */
bool cxpr_ir_prepare_defined_program(cxpr_func_entry* entry, const cxpr_registry* reg,
                                     cxpr_error* err);
/** @brief Evaluate an internal IR program against a context and registry. */
double cxpr_ir_exec(const cxpr_ir_program* program, const cxpr_context* ctx,
                    const cxpr_registry* reg, cxpr_error* err);
/** @brief Evaluate an IR program with an optional local-slot frame. */
double cxpr_ir_exec_with_locals(const cxpr_ir_program* program, const cxpr_context* ctx,
                                const cxpr_registry* reg, const double* locals,
                                size_t local_count, cxpr_error* err);
const char* cxpr_ir_opcode_name(cxpr_opcode op);

/* ═══════════════════════════════════════════════════════════════════════════
 * Formula engine structure
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief A single formula entry in the engine.
 */
typedef struct {
    char* name;               /**< Formula name, owned */
    char* expression;         /**< Original expression string, owned */
    cxpr_ast* ast;              /**< Parsed AST (NULL until compiled) */
    cxpr_program* program;      /**< Compiled program cache (NULL until compiled) */
    double result;            /**< Evaluation result */
    bool evaluated;
} cxpr_formula_entry;

#define CXPR_FORMULA_INITIAL_CAPACITY 32

struct cxpr_formula_engine {
    cxpr_formula_entry* formulas;
    size_t capacity;
    size_t count;
    size_t* eval_order;       /**< Indices in topological order */
    size_t eval_order_count;
    bool compiled;
    const cxpr_registry* registry; /**< Borrowed reference */
    cxpr_parser* parser;        /**< Internal parser for formula expressions */
};

#endif /* CXPR_INTERNAL_H */
