#include "horizontal_microcode.h"
#include "cpu.h"
#include <stdexcept>
#include <iostream>
#include <sstream>

using namespace risc201;

namespace {
    // A condensed summary of all hardware signals needed for an instruction
    struct MicroProfile {
        OperationCode op;
        // Register reads (ID/RR)
        bool rs1, rs2, rd, ra, un;
        // Execution (EX)
        bool alu;
        OperationCode aluOp;
        bool flg, tdz, tov;
        BranchControl branch;
        bool link, destIsRA, stop;
        // Memory & Writeback (MEM/WB)
        bool mr, mw, rw, halt;
    };

    // The Master Control Profile Table
    const MicroProfile kProfiles[] = {
        // op            rs1, rs2, rd, ra, un, alu, aluOp,             flg, tdz, tov, branch,               link, destRA, stop, mr, mw, rw, halt
        { OperationCode::ADD,  1, 1, 0, 0, 0,  1, OperationCode::ADD,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::SUB,  1, 1, 0, 0, 0,  1, OperationCode::SUB,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::MUL,  1, 1, 0, 0, 0,  1, OperationCode::MUL,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::DIV,  1, 1, 0, 0, 0,  1, OperationCode::DIV,  0,   1,   1,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::MOD,  1, 1, 0, 0, 0,  1, OperationCode::MOD,  0,   1,   1,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        
        { OperationCode::CMP,  1, 1, 0, 0, 0,  1, OperationCode::CMP,  1,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  0,  0 },
        
        { OperationCode::AND,  1, 1, 0, 0, 0,  1, OperationCode::AND,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::OR,   1, 1, 0, 0, 0,  1, OperationCode::OR,   0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::XOR,  1, 1, 0, 0, 0,  1, OperationCode::XOR,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::NOT,  1, 0, 0, 0, 1,  1, OperationCode::NOT,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::MOV,  0, 1, 0, 0, 0,  1, OperationCode::MOV,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        
        { OperationCode::LSL,  1, 1, 0, 0, 0,  1, OperationCode::LSL,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::LSR,  1, 1, 0, 0, 0,  1, OperationCode::LSR,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::ASR,  1, 1, 0, 0, 0,  1, OperationCode::ASR,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::ROL,  1, 1, 0, 0, 0,  1, OperationCode::ROL,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::ROR,  1, 1, 0, 0, 0,  1, OperationCode::ROR,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        
        { OperationCode::NOP,  0, 0, 0, 0, 0,  0, OperationCode::NOP,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  0,  0 },
        
        { OperationCode::LD,   1, 1, 0, 0, 0,  1, OperationCode::ADD,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    1,  0,  1,  0 },
        { OperationCode::ST,   1, 1, 1, 0, 0,  1, OperationCode::ADD,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  1,  0,  0 },
        
        { OperationCode::BEQ,  0, 0, 0, 0, 0,  0, OperationCode::NOP,  0,   0,   0,   BranchControl::COND_EQ, 0,    0,      0,    0,  0,  0,  0 },
        { OperationCode::BGT,  0, 0, 0, 0, 0,  0, OperationCode::NOP,  0,   0,   0,   BranchControl::COND_GT, 0,    0,      0,    0,  0,  0,  0 },
        { OperationCode::B,    0, 0, 0, 0, 0,  0, OperationCode::NOP,  0,   0,   0,   BranchControl::UNCOND,  0,    0,      0,    0,  0,  0,  0 },
        { OperationCode::CALL, 0, 0, 0, 0, 0,  0, OperationCode::NOP,  0,   0,   0,   BranchControl::CALL,    1,    1,      0,    0,  0,  1,  0 },
        { OperationCode::RET,  0, 0, 0, 1, 0,  0, OperationCode::NOP,  0,   0,   0,   BranchControl::RET,     0,    0,      0,    0,  0,  0,  0 },
        
        { OperationCode::NEG,  1, 0, 0, 0, 1,  1, OperationCode::NEG,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::ABS,  1, 0, 0, 0, 1,  1, OperationCode::ABS,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::MIN,  1, 1, 0, 0, 0,  1, OperationCode::MIN,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        { OperationCode::MAX,  1, 1, 0, 0, 0,  1, OperationCode::MAX,  0,   0,   0,   BranchControl::NONE,    0,    0,      0,    0,  0,  1,  0 },
        
        { OperationCode::HALT, 0, 0, 0, 0, 0,  0, OperationCode::NOP,  0,   0,   0,   BranchControl::NONE,    0,    0,      1,    0,  0,  0,  1 }
    };
} // namespace

void buildControlStore(PipelineMode mode, MicroWord rom[], uint8_t dispatch[32]) {
    // 1. Initialize empty ROM
    for (size_t i = 0; i < CONTROL_STORE_SIZE; i++) {
        rom[i].valid = false;
    }
    
    // 2. Address 0 is the universal FETCH microword
    rom[0].valid = true;
    rom[0].stage = MicroStage::IF;
    rom[0].seq = Seq::END; 

    uint8_t nextFree = 1;

    // 3. Expand each profile row into sequential microwords
    for (const auto& prof : kProfiles) {
        uint8_t opIdx = static_cast<uint8_t>(prof.op);
        dispatch[opIdx] = nextFree; // Save entry point for dispatcher

        if (mode == PipelineMode::STAGE_4) {
            // Word 1: ID (Register Reads)
            MicroWord idWord;
            idWord.valid = true;
            idWord.stage = MicroStage::ID;
            idWord.readRs1 = prof.rs1; idWord.readRs2 = prof.rs2;
            idWord.readRd = prof.rd; idWord.readRa = prof.ra; idWord.unaryOp = prof.un;
            rom[nextFree++] = idWord;

            // Word 2: EX (ALU & Branch)
            MicroWord exWord;
            exWord.valid = true;
            exWord.stage = MicroStage::EX;
            exWord.aluEnable = prof.alu; exWord.aluOp = prof.aluOp;
            exWord.updateFlags = prof.flg; exWord.trapDivZero = prof.tdz; exWord.trapOverflow = prof.tov;
            exWord.branch = prof.branch; exWord.linkPC = prof.link; exWord.destIsRA = prof.destIsRA;
            exWord.stopFetch = prof.stop;
            rom[nextFree++] = exWord;

            // Word 3: MEMWB (Memory & Writeback)
            MicroWord memwbWord;
            memwbWord.valid = true;
            memwbWord.stage = MicroStage::MEMWB;
            memwbWord.memRead = prof.mr; memwbWord.memWrite = prof.mw;
            memwbWord.regWrite = prof.rw; memwbWord.halt = prof.halt;
            memwbWord.seq = Seq::END; // Terminate sequence
            rom[nextFree++] = memwbWord;

        } else if (mode == PipelineMode::STAGE_6) {
            // Word 1: ID (Decode Only)
            MicroWord idWord;
            idWord.valid = true; idWord.stage = MicroStage::ID;
            rom[nextFree++] = idWord;

            // Word 2: RR (Register Read)
            MicroWord rrWord;
            rrWord.valid = true; rrWord.stage = MicroStage::RR;
            rrWord.readRs1 = prof.rs1; rrWord.readRs2 = prof.rs2;
            rrWord.readRd = prof.rd; rrWord.readRa = prof.ra; rrWord.unaryOp = prof.un;
            rom[nextFree++] = rrWord;

            // Word 3: EX (Execution)
            MicroWord exWord;
            exWord.valid = true; exWord.stage = MicroStage::EX;
            exWord.aluEnable = prof.alu; exWord.aluOp = prof.aluOp;
            exWord.updateFlags = prof.flg; exWord.trapDivZero = prof.tdz; exWord.trapOverflow = prof.tov;
            exWord.branch = prof.branch; exWord.linkPC = prof.link; exWord.destIsRA = prof.destIsRA;
            exWord.stopFetch = prof.stop;
            rom[nextFree++] = exWord;

            // Word 4: MEM (Memory)
            MicroWord memWord;
            memWord.valid = true; memWord.stage = MicroStage::MEM;
            memWord.memRead = prof.mr; memWord.memWrite = prof.mw;
            rom[nextFree++] = memWord;

            // Word 5: WB (Writeback)
            MicroWord wbWord;
            wbWord.valid = true; wbWord.stage = MicroStage::WB;
            wbWord.regWrite = prof.rw; wbWord.halt = prof.halt;
            wbWord.seq = Seq::END; // Terminate sequence
            rom[nextFree++] = wbWord;
        }

        // Validate the newly built routine
        int endCount = 0;
        for (uint8_t i = dispatch[opIdx]; i < nextFree; i++) {
            std::string err = validateMicroWord(rom[i], rom[i].stage);
            if (!err.empty()) {
                throw std::runtime_error("Microcode validation failed for op " + std::to_string(opIdx) + ": " + err);
            }
            if (rom[i].seq == Seq::END) endCount++;
        }
        if (endCount != 1) {
            throw std::runtime_error("Microcode routine for op " + std::to_string(opIdx) + " must have exactly one END word.");
        }
    }
}

std::string validateMicroWord(const MicroWord& word, MicroStage stage) {
    if (!word.valid) return "Microword is invalid.";
    
    // Ensure signals only activate in their correct hardware stages
    if (stage != MicroStage::ID && stage != MicroStage::RR) {
        if (word.readRs1 || word.readRs2 || word.readRd || word.readRa || word.unaryOp) 
            return "Read signals asserted outside of ID/RR stage.";
    }
    if (stage != MicroStage::EX) {
        if (word.aluEnable || word.updateFlags || word.trapDivZero || word.trapOverflow || 
            word.branch != BranchControl::NONE || word.linkPC || word.stopFetch)
            return "Execution signals asserted outside of EX stage.";
    }
    if (stage != MicroStage::MEMWB && stage != MicroStage::MEM && stage != MicroStage::WB) {
        if (word.memRead || word.memWrite || word.regWrite || word.halt)
            return "Memory/Writeback signals asserted early.";
    }
    if (word.memRead && word.memWrite) return "Simultaneous Memory Read and Write asserted.";
    return "";
}


std::string describe(const MicroWord& word) {
    if (!word.valid) return "[INVALID]";
    std::ostringstream out;
    out << "[";
    switch(word.stage) {
        case MicroStage::IF: out << "IF"; break;
        case MicroStage::ID: out << "ID"; break;
        case MicroStage::RR: out << "RR"; break;
        case MicroStage::EX: out << "EX"; break;
        case MicroStage::MEM: out << "MEM"; break;
        case MicroStage::WB: out << "WB"; break;
        case MicroStage::MEMWB: out << "MEMWB"; break;
    }


 out << "] ";
    
    if (word.readRs1) out << "rdRs1 ";
    if (word.readRs2) out << "rdRs2 ";
    if (word.readRd) out << "rdRd ";
    if (word.readRa) out << "rdRa ";
    if (word.aluEnable) out << "ALU=ON ";
    if (word.updateFlags) out << "updFlg ";
    if (word.memRead) out << "memRD ";
    if (word.memWrite) out << "memWR ";
    if (word.regWrite) out << "regWR ";
    if (word.branch != BranchControl::NONE) out << "BRANCH ";
    if (word.seq == Seq::END) out << "(END)";
    return out.str();
}


   
