# Verifiable Compilation Proof-Of-Concept

This sample shows how to drive the RISC Zero zkVM with a C guest that compiles
other C code.  The guest embeds a lightly modified copy of
[chibicc](https://github.com/rui314/chibicc).  When the host provides a source
file (`main.c` by default) the guest parses it, emits x86-64 style assembly, and
commits both the input and generated output to the zkVM journal.  A separate
verifier binary demonstrates how to check the resulting receipt and recover the
assembly listing.

## Project Layout

- `guest/` – C sources for the guest program.  `main.c` creates the execution
  environment, invokes the tokenizer, parser, and code generator (`tokenize.c`,
  `parse.c`, `type.c`, `codegen.c`), and streams the resulting log back to the
  host.
- `platform/` – Rust crate compiled to a static library that provides the shim
  used by the C guest (`init_allocator`, `env_read`, `env_commit`, `env_exit`,
  SHA-256 helpers, etc.).
- `host/` – Rust binary that builds the guest image, feeds the guest with
  `main.c`, and writes the resulting receipt to `out/receipt.bin`.
- `verifier/` – Rust binary that loads the stored receipt, verifies it against
  the computed image ID, prints the journal contents, and saves the generated
  assembly to `program.s`.
- `main.c` (workspace root) – Sample program compiled by the guest during the
  demo.

## Prerequisites

- [rustup] with the RISC Zero toolchain installed (`rzup install rust`).
- `cargo` available in your shell (set automatically by `rzup`).
- Optional: `cbindgen` if you intend to regenerate the guest header via
  `make headers`.

> **Note:** The build scripts use `rzup` to locate the appropriate toolchain.
> Ensure `rzup` is on your `PATH` before running the commands below.

## Quick Start

The `Makefile` mirrors the logic in the Cargo build scripts and is convenient
for end-to-end runs.

```bash
# Build the guest image, execute inside the zkVM, and emit a receipt in dev mode
make execute

# Produce an attested receipt (same as execute, but without dev mode shortcuts)
make prove

# Verify an existing receipt and write the generated assembly to program.s
make verify

# Assemble output program
make assemble
```

Behind the scenes these steps:

1. Compile the `zkvm-platform` crate to a static library (`platform/src/lib.rs`)
   so that the guest can call into the zkVM syscalls.
2. Build the C guest sources into `guest/out/main`.
3. Run the host binary (`c-guest-host`) which reads `./main.c`, writes it to the
   guest via `ExecutorEnv`, and saves the resulting receipt.
4. (For `make verify`) Run the verifier binary to recompute the image ID using
   `ProgramBinary`, validate the receipt, and export the journal payload to
   `program.s`.

You can also drive the components manually with Cargo:

```bash
# Build everything (host build.rs handles the guest compilation)
cargo run -p c-guest-host

# After a receipt exists in out/receipt.bin
cargo run -p verifier
```

## Customizing the Input Program

Edit the workspace-level `main.c` to change the program compiled by the guest.
When you rerun `make execute` or `cargo run -p c-guest-host`, the host reloads
the file, the guest recompiles it deterministically, and the verifier will emit
the new assembly listing.  The guest input buffer is limited to 256 bytes, so
keep the example small or update `guest/main.c` if you need more room.

## Outputs

- `out/receipt.bin` – Binary-encoded receipt saved by the host.
- `program.s` – Assembly recovered by the verifier from the journal payload.
- Journal contents also include the original source (first 256 bytes) followed
  by the log buffer produced by `codegen.c`.

## Troubleshooting

- Ensure `rzup` has installed the Rust toolchain and that the `cargo +risc0`
  targets are available; otherwise the host build script will panic.
- If you regenerate headers with `make headers`, run `make execute` afterward
  to rebuild the guest with the updated platform bindings.
- Receipts must be generated (`make execute` or `make prove`) before running
  `make verify`.

[rustup]: https://rustup.rs
