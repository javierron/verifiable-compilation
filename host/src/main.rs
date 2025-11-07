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

use std::fs;

use risc0_binfmt::ProgramBinary;
use risc0_zkos_v1compat::V1COMPAT_ELF;
use risc0_zkvm::{compute_image_id, default_prover, ExecutorEnv};


fn main() -> anyhow::Result<()> {
    // Initialize tracing. In order to view logs, run `RUST_LOG=info cargo run`
    tracing_subscriber::fmt()
        .with_env_filter(tracing_subscriber::filter::EnvFilter::from_default_env())
        .init();

    // Load built gcc program and compute it's image ID.
    // TODO have the image ID be calculated at compile time, to avoid potential vulnerabilities
    let guest_elf = fs::read("./guest/out/main")?;
    let program_binary = ProgramBinary::new(&guest_elf, V1COMPAT_ELF);
    let consensus_elf = program_binary.encode();

    // Read program source from file
    let program_source = fs::read_to_string("./main.c")?;
    let bytes_i8: Vec<i8> = program_source.as_bytes().iter().map(|&b| b as i8).collect();

    let env = ExecutorEnv::builder()
        .write_slice(&bytes_i8)
        .build()
        .unwrap();
    let prover = default_prover();

    // Produce a receipt by proving the specified ELF binary.
    let receipt = prover.prove(env, &consensus_elf)?.receipt;

    // Save the receipt to a file for offline verification
    let receipt_bytes = bincode::serialize(&receipt)?;
    fs::create_dir_all("out").ok();
    fs::write("out/receipt.bin", receipt_bytes)?;

    Ok(())
}
