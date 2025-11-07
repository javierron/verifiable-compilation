#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct Type Type;
typedef struct Node Node;

//
// tokenize.c
//

// Token
typedef enum {
  TK_IDENT,   // Identifiers
  TK_PUNCT,   // Punctuators
  TK_KEYWORD, // Keywords
  TK_STR,     // String literals
  TK_NUM,     // Numeric literals
  TK_EOF,     // End-of-file markers
} TokenKind;

// Token type
typedef struct Token Token;
struct Token {
  TokenKind kind; // Token kind
  Token *next;    // Next token
  int val;        // If kind is TK_NUM, its value
  char *loc;      // Token location
  int len;        // Token length
  Type *ty;       // Used if TK_STR
  char *str;      // String literal contents including terminating '\0'
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
Token *skip(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *str);
Token *tokenize(char *input);

//
// parse.c
//

// Variable or function
typedef struct Obj Obj;
struct Obj {
  Obj *next;
  char *name;    // Variable name
  Type *ty;      // Type
  bool is_local; // local or global/function

  // Local variable
  int offset;

  // Global variable or function
  bool is_function;

  // Global variable
  char *init_data;

  // Function
  Obj *params;
  Node *body;
  Obj *locals;
  int stack_size;
};

// AST node
typedef enum {
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_NEG,       // unary -
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=
  ND_ASSIGN,    // =
  ND_ADDR,      // unary &
  ND_DEREF,     // unary *
  ND_RETURN,    // "return"
  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_BLOCK,     // { ... }
  ND_FUNCALL,   // Function call
  ND_EXPR_STMT, // Expression statement
  ND_VAR,       // Variable
  ND_NUM,       // Integer
} NodeKind;

// AST node type
struct Node {
  NodeKind kind; // Node kind
  Node *next;    // Next node
  Type *ty;      // Type, e.g. int or pointer to int
  Token *tok;    // Representative token

  Node *lhs;     // Left-hand side
  Node *rhs;     // Right-hand side

  // "if" or "for" statement
  Node *cond;
  Node *then;
  Node *els;
  Node *init;
  Node *inc;

  // Block
  Node *body;

  // Function call
  char *funcname;
  Node *args;

  Obj *var;      // Used if kind == ND_VAR
  int val;       // Used if kind == ND_NUM
};

Obj *parse(Token *tok);

//
// type.c
//

typedef enum {
  TY_CHAR,
  TY_INT,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
} TypeKind;

struct Type {
  TypeKind kind;
  int size;      // sizeof() value

  // Pointer-to or array-of type. We intentionally use the same member
  // to represent pointer/array duality in C.
  //
  // In many contexts in which a pointer is expected, we examine this
  // member instead of "kind" member to determine whether a type is a
  // pointer or not. That means in many contexts "array of T" is
  // naturally handled as if it were "pointer to T", as required by
  // the C spec.
  Type *base;

  // Declaration
  Token *name;

  // Array
  int array_len;

  // Function type
  Type *return_ty;
  Type *params;
  Type *next;
};

extern Type *ty_char;
extern Type *ty_int;

bool is_integer(Type *ty);
Type *copy_type(Type *ty);
Type *pointer_to(Type *base);
Type *func_type(Type *return_ty);
Type *array_of(Type *base, int size);
void add_type(Node *node);

//
// codegen.c
//

void codegen(Obj *prog);

// --- codegen logging buffer API ---
// Appends formatted text to the codegen log buffer (truncated on overflow).
void cg_log_print(const char *fmt, ...);
// Returns a pointer to the current log buffer bytes.
const unsigned char *cg_log_buffer(void);
// Returns the number of valid bytes currently stored in the buffer.
size_t cg_log_size(void);
// Clears the buffer to empty.
void cg_log_reset(void);

// --- Simple per-translation-unit arena to avoid heap allocations ---
// The arena is intentionally static so each .c gets its own pool.
// Size can be tuned based on expected input/program size.
static unsigned char __arena_buffer[128 * 1024];
static size_t __arena_offset;

static void *__arena_alloc(size_t size, size_t align, int zero_init) {
  uintptr_t base = (uintptr_t)__arena_buffer;
  uintptr_t curr = base + __arena_offset;
  uintptr_t aligned = (curr + (align - 1)) & ~(uintptr_t)(align - 1);
  size_t padding = (size_t)(aligned - curr);
  if (aligned + size > base + sizeof(__arena_buffer)) {
    // Out of arena memory; fail fast
    // fprintf(stderr, "arena exhausted (need %zu bytes)\n", size);
    exit(1);
  }
  __arena_offset += padding + size;
  void *ptr = (void *)aligned;
  if (zero_init)
    memset(ptr, 0, size);
  return ptr;
}

static void *xcalloc(size_t n, size_t size) {
  // Use 8-byte alignment for general objects
  size_t total = n * size;
  return __arena_alloc(total, 8, 1);
}

static char *xstrndup(const char *s, size_t n) {
  char *p = (char *)__arena_alloc(n + 1, 1, 0);
  memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

static char *xstrdup(const char *s) {
  return xstrndup(s, strlen(s));
}
