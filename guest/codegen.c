#include "chibicc.h"

static int depth;
static char *argreg8[] = {"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"};
static char *argreg64[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
static Obj *current_fn;

static void gen_expr(Node *node);

// --- Static codegen logging buffer ---
// We store all emitted text into this buffer for later retrieval.
static unsigned char __cg_buf[64 * 1024];
static size_t __cg_len;

static void __cg_append_bytes(const unsigned char *src, size_t n) {
  if (n == 0)
    return;
  size_t remaining = sizeof(__cg_buf) - __cg_len;
  size_t to_copy = n < remaining ? n : remaining;
  if (to_copy) {
    memcpy(__cg_buf + __cg_len, src, to_copy);
    __cg_len += to_copy;
  }
}

static void __cg_vprint(const char *fmt, va_list ap) {
  char tmp[1024];
  int written = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  if (written <= 0)
    return;
  size_t n = (size_t)written;
  if (n >= sizeof(tmp))
    n = sizeof(tmp) - 1;
  __cg_append_bytes((const unsigned char *)tmp, n);
}

void cg_log_print(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  __cg_vprint(fmt, ap);
  va_end(ap);
}

const unsigned char *cg_log_buffer(void) { return __cg_buf; }
size_t cg_log_size(void) { return __cg_len; }
void cg_log_reset(void) { __cg_len = 0; }

static int count(void) {
  static int i = 1;
  return i++;
}

static void push(void) {
  cg_log_print("  push %%rax\n");
  depth++;
}

static void pop(char *arg) {
  cg_log_print("  pop %s\n", arg);
  depth--;
}

// Round up `n` to the nearest multiple of `align`. For instance,
// align_to(5, 8) returns 8 and align_to(11, 8) returns 16.
static int align_to(int n, int align) {
  return (n + align - 1) / align * align;
}

// Compute the absolute address of a given node.
// It's an error if a given node does not reside in memory.
static void gen_addr(Node *node) {
  switch (node->kind) {
  case ND_VAR:
    if (node->var->is_local) {
      // Local variable
      cg_log_print("  lea %d(%%rbp), %%rax\n", node->var->offset);
    } else {
      // Global variable
      cg_log_print("  lea %s(%%rip), %%rax\n", node->var->name);
    }
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "not an lvalue");
}

// Load a value from where %rax is pointing to.
static void load(Type *ty) {
  if (ty->kind == TY_ARRAY) {
    // If it is an array, do not attempt to load a value to the
    // register because in general we can't load an entire array to a
    // register. As a result, the result of an evaluation of an array
    // becomes not the array itself but the address of the array.
    // This is where "array is automatically converted to a pointer to
    // the first element of the array in C" occurs.
    return;
  }

  if (ty->size == 1)
    cg_log_print("  movsbq (%%rax), %%rax\n");
  else
    cg_log_print("  mov (%%rax), %%rax\n");
}

// Store %rax to an address that the stack top is pointing to.
static void store(Type *ty) {
  pop("%rdi");

  if (ty->size == 1)
    cg_log_print("  mov %%al, (%%rdi)\n");
  else
    cg_log_print("  mov %%rax, (%%rdi)\n");
}

// Generate code for a given node.
static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_NUM:
    cg_log_print("  mov $%d, %%rax\n", node->val);
    return;
  case ND_NEG:
    gen_expr(node->lhs);
    cg_log_print("  neg %%rax\n");
    return;
  case ND_VAR:
    gen_addr(node);
    load(node->ty);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_ASSIGN:
    gen_addr(node->lhs);
    push();
    gen_expr(node->rhs);
    store(node->ty);
    return;
  case ND_FUNCALL: {
    int nargs = 0;
    for (Node *arg = node->args; arg; arg = arg->next) {
      gen_expr(arg);
      push();
      nargs++;
    }

    for (int i = nargs - 1; i >= 0; i--)
      pop(argreg64[i]);

    cg_log_print("  mov $0, %%rax\n");
    cg_log_print("  call %s\n", node->funcname);
    return;
  }
  }

  gen_expr(node->rhs);
  push();
  gen_expr(node->lhs);
  pop("%rdi");

  switch (node->kind) {
  case ND_ADD:
    cg_log_print("  add %%rdi, %%rax\n");
    return;
  case ND_SUB:
    cg_log_print("  sub %%rdi, %%rax\n");
    return;
  case ND_MUL:
    cg_log_print("  imul %%rdi, %%rax\n");
    return;
  case ND_DIV:
    cg_log_print("  cqo\n");
    cg_log_print("  idiv %%rdi\n");
    return;
  case ND_EQ:
  case ND_NE:
  case ND_LT:
  case ND_LE:
    cg_log_print("  cmp %%rdi, %%rax\n");

    if (node->kind == ND_EQ)
      cg_log_print("  sete %%al\n");
    else if (node->kind == ND_NE)
      cg_log_print("  setne %%al\n");
    else if (node->kind == ND_LT)
      cg_log_print("  setl %%al\n");
    else if (node->kind == ND_LE)
      cg_log_print("  setle %%al\n");

    cg_log_print("  movzb %%al, %%rax\n");
    return;
  }

  error_tok(node->tok, "invalid expression");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_IF: {
    int c = count();
    gen_expr(node->cond);
    cg_log_print("  cmp $0, %%rax\n");
    cg_log_print("  je  .L.else.%d\n", c);
    gen_stmt(node->then);
    cg_log_print("  jmp .L.end.%d\n", c);
    cg_log_print(".L.else.%d:\n", c);
    if (node->els)
      gen_stmt(node->els);
    cg_log_print(".L.end.%d:\n", c);
    return;
  }
  case ND_FOR: {
    int c = count();
    if (node->init)
      gen_stmt(node->init);
    cg_log_print(".L.begin.%d:\n", c);
    if (node->cond) {
      gen_expr(node->cond);
      cg_log_print("  cmp $0, %%rax\n");
      cg_log_print("  je  .L.end.%d\n", c);
    }
    gen_stmt(node->then);
    if (node->inc)
      gen_expr(node->inc);
    cg_log_print("  jmp .L.begin.%d\n", c);
    cg_log_print(".L.end.%d:\n", c);
    return;
  }
  case ND_BLOCK:
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    return;
  case ND_RETURN:
    gen_expr(node->lhs);
    cg_log_print("  jmp .L.return.%s\n", current_fn->name);
    return;
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    return;
  }

  error_tok(node->tok, "invalid statement");
}

// Assign offsets to local variables.
static void assign_lvar_offsets(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    int offset = 0;
    for (Obj *var = fn->locals; var; var = var->next) {
      offset += var->ty->size;
      var->offset = -offset;
    }
    fn->stack_size = align_to(offset, 16);
  }
}

static void emit_data(Obj *prog) {
  for (Obj *var = prog; var; var = var->next) {
    if (var->is_function)
      continue;

    cg_log_print("  .data\n");
    cg_log_print("  .globl %s\n", var->name);
    cg_log_print("%s:\n", var->name);

    if (var->init_data) {
      for (int i = 0; i < var->ty->size; i++)
        cg_log_print("  .byte %d\n", var->init_data[i]);
    } else {
      cg_log_print("  .zero %d\n", var->ty->size);
    }
  }
}

static void emit_text(Obj *prog) {
  for (Obj *fn = prog; fn; fn = fn->next) {
    if (!fn->is_function)
      continue;

    cg_log_print("  .globl %s\n", fn->name);
    cg_log_print("  .text\n");
    cg_log_print("%s:\n", fn->name);
    current_fn = fn;

    // Prologue
    cg_log_print("  push %%rbp\n");
    cg_log_print("  mov %%rsp, %%rbp\n");
    cg_log_print("  sub $%d, %%rsp\n", fn->stack_size);

    // Save passed-by-register arguments to the stack
    int i = 0;
    for (Obj *var = fn->params; var; var = var->next) {
      if (var->ty->size == 1)
        cg_log_print("  mov %s, %d(%%rbp)\n", argreg8[i++], var->offset);
      else
        cg_log_print("  mov %s, %d(%%rbp)\n", argreg64[i++], var->offset);
    }

    // Emit code
    gen_stmt(fn->body);
    assert(depth == 0);

    // Epilogue
    cg_log_print(".L.return.%s:\n", fn->name);
    cg_log_print("  mov %%rbp, %%rsp\n");
    cg_log_print("  pop %%rbp\n");
    cg_log_print("  ret\n");
  }
}

void codegen(Obj *prog) {
  cg_log_reset();
  assign_lvar_offsets(prog);
  emit_data(prog);
  emit_text(prog);
}
