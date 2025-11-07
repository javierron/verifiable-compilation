// Copyright 2024 RISC Zero, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "out/platform/platform.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include "chibicc.h"

int main() {
  init_allocator();
  // TODO introduce entropy into memory image (for zk)
  sha256_state* hasher = init_sha256();

  // Read two u32 values from the host, assuming LE byte order.
  uint8_t in_buffer[256 * 1];
  size_t read_len = env_read(in_buffer, sizeof(in_buffer));
  if (read_len >= sizeof(in_buffer)) {
    read_len = sizeof(in_buffer) - 1;
  }
  in_buffer[read_len] = '\0';
  if (read_len + 1 < sizeof(in_buffer)) {
    memset(in_buffer + read_len + 1, 0, sizeof(in_buffer) - (read_len + 1));
  }


  Token *tok = tokenize(in_buffer);
  Obj *prog = parse(tok);

  // Traverse the AST to emit assembly.
  codegen(prog);


  uint8_t out_buffer[256 * 16];
  // Copy codegen's log buffer into out_buffer (bounded)
  memset(out_buffer, 0, sizeof(out_buffer));
  memcpy(out_buffer, in_buffer, sizeof(in_buffer));

  const unsigned char *cg_buf = cg_log_buffer();
  size_t cg_len = cg_log_size();
  size_t remaining = sizeof(out_buffer) - sizeof(in_buffer);
  if (cg_len > remaining)
    cg_len = remaining;
  memcpy(out_buffer + sizeof(in_buffer), cg_buf, cg_len);

  size_t journal_len = sizeof(in_buffer) + cg_len;
  env_commit(hasher, out_buffer, (uint32_t)journal_len);
  env_exit(hasher, 0);

  return 0;
}
