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
use risc0_zkvm::{compute_image_id, Receipt};



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
    let consensus_id = compute_image_id(&consensus_elf)?;

    // Read receipt from file and verify it
    let receipt_bytes = fs::read("out/receipt.bin")?;
    let receipt: Receipt = bincode::deserialize(&receipt_bytes)?;
    receipt.verify(consensus_id)?;
    
    println!("{:?}", receipt.claim()?.value()?.input.value());


    println!("receipt verified, printing output to program.s");
    // The default serialization for u32 is to (de)serialize as le bytes, so this will match
    // the format committed from the guest.
    let return_value: Vec<u8> = receipt.journal.bytes;

    const INPUT_BUFFER_LEN: usize = 256;
    let input_chunk_len = INPUT_BUFFER_LEN.min(return_value.len());
    let (input_bytes, remainder) = return_value.split_at(input_chunk_len);
    let end_of_input = input_bytes
        .iter()
        .position(|&byte| byte == 0)
        .unwrap_or(input_bytes.len());
    let input_str = String::from_utf8_lossy(&input_bytes[..end_of_input]);
    println!("guest input:\n{}", input_str);

    let mut remainder_bytes = remainder.to_vec();
    while matches!(remainder_bytes.last(), Some(&0)) {
        remainder_bytes.pop();
    }
    let remainder_str = String::from_utf8_lossy(&remainder_bytes);
    fs::write("program.s", remainder_str.as_bytes())?;

    Ok(())
}
