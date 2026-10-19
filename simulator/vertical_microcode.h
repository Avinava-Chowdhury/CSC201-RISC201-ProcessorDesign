#pragma once
// vertical_microcode.h -- VERTICAL (encoded) control unit for RISC201.
//
// The horizontal control store (horizontal_microcode.h) spends one ROM bit per control line
// (29 bits per word). The vertical control store packs the SAME microprogram into 16-bit words
// by grouping mutually exclusive control lines into small encoded fields. The price is a decode
// layer (decodeMicroWord) between the ROM output and the pipeline stages.
//
//   15 14 13 | 12  | 11 ............................ 0
//   STAGE(3) | END | PAYLOAD (meaning depends on STAGE)
//
//   STAGE   0=IF 1=ID 2=RR 3=EX 4=MEM 5=WB 6=MEMWB   7 = empty ROM location (invalid)
//   END     1 = last word of the routine (Seq::END), 0 = Seq::NEXT
//
//   PAYLOAD for ID / RR  (register-read stage)
//     [2:0] RDSEL  0 none | 1 rs1 | 2 rs1+rs2 | 3 rs1+unary | 4 rs1+rs2+rd (store) | 5 ra (ret)
//   PAYLOAD for EX
//     [11:7] ALU   ALU operation (OperationCode); NOP (13) = ALU disabled
//     [ 6:4] BR    BranchControl: 0 none 1 beq 2 bgt 3 b 4 call 5 ret
//     [ 3:2] FLG   0 none | 1 update flags | 2 trap div-by-zero + overflow
//     [ 1:0] LNK   0 none | 1 call link (linkPC + destIsRA) | 2 stop fetch
//   PAYLOAD for MEM / WB / MEMWB
//     [2:0] MSEL   0 none | 1 reg write | 2 load (mem read + reg write) | 3 store (mem write) | 4 halt
//   PAYLOAD for IF: unused (0)

#include <cstdint>
#include <string>
#include "horizontal_microcode.h"

using VMicroWord = uint16_t;

constexpr int HORIZONTAL_WORD_BITS = 29;   // valid, stage(3), seq, 5 read, 4 ex-bool, 3 link/stop, alu(5), branch(3), 4 mem/wb
constexpr int VERTICAL_WORD_BITS   = 16;
constexpr VMicroWord VROM_EMPTY    = 0xE000;   // stage field = 7 -> no microword at this address

// Encode one horizontal microword into the vertical format. Throws std::logic_error if the
// word uses a combination of control lines the encoded format cannot express.
VMicroWord encodeMicroWord(const MicroWord& w);

// THE DECODE LAYER: expand a vertical word back into the control lines the pipeline stages use.
// If the word contains an undefined field value, the result has valid == false and *err says why.
// An empty location (VROM_EMPTY) also gives valid == false but leaves *err empty.
MicroWord decodeMicroWord(VMicroWord v, std::string* err = nullptr);

// Second control store: same microprogram, encoded. dispatch[] is filled identically to the
// horizontal build (same entry addresses), so the two stores are interchangeable.
void buildVerticalStore(PipelineMode mode, VMicroWord rom[], uint8_t dispatch[32]);

// Decodes every vertical word and compares it with the horizontal ROM, field by field.
// Returns "" when the two stores are equivalent, otherwise a description of the first mismatch.
std::string verifyVerticalStore(const MicroWord hrom[], const VMicroWord vrom[]);

// Text helpers for the debugger
std::string describeVertical(VMicroWord v);            // raw hex + field breakdown, e.g. "0x1B64 ..."
std::string vfieldBits(VMicroWord v);                  // 16-bit binary string grouped as stage|end|payload
