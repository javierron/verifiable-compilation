  .globl main
  .text
main:
  push %rbp
  mov %rsp, %rbp
  sub $16, %rsp
  lea -16(%rbp), %rax
  push %rax
  mov $21, %rax
  pop %rdi
  mov %rax, (%rdi)
  lea -8(%rbp), %rax
  push %rax
  mov $2, %rax
  pop %rdi
  mov %rax, (%rdi)
  lea -8(%rbp), %rax
  mov (%rax), %rax
  push %rax
  lea -16(%rbp), %rax
  mov (%rax), %rax
  pop %rdi
  imul %rdi, %rax
  jmp .L.return.main
.L.return.main:
  mov %rbp, %rsp
  pop %rbp
  ret
