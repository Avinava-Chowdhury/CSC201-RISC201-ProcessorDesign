#include "cpu.h"
#include "sim_exception.h"
#include <fstream>
#include <string>

// CPU Constructor & Reset
CPU::CPU() {
    instrMem.assign(4096, 0); 
    dataMem.assign(65536, 0); 
    reset();
}

void CPU::reset() {
    for (int i = 0; i < 16; i++) {
        reg[i] = 0;
    }
    // sp (r14) starts at 0x10000 since stack grows downwards
    reg[risc201::STACK_POINTER_REG_INDEX] = risc201::STACK_STARTING_ADDRESS; 
    pc = 0;
    
    // Clear flags
    flags.IsEqual = false;
    flags.IsGreaterThan = false;

    // Flush/Clear pipeline latches
    if_id = IF_ID_Latch{};
    id_ex = ID_EX_Latch{};
    ex_memwb = EX_MEMWB_Latch{};

    // Program the horizontal control ROM
    loadControlStore();
}
// CPU Hex Loader
bool CPU::loadHex(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    uint32_t memIndex = 0;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (memIndex >= 4096) break; 
        
        uint32_t instruction = std::stoul(line, nullptr, 16);
        instrMem[memIndex++] = instruction;
    }
    return true;
}

// Microcode Control Store (ROM) Initialization
void CPU::loadControlStore() {
    // 1. Safe NOP default for unmapped slots
    MicroInstruction nop = {
        .regWrite    = false,
        .memRead     = false,
        .memWrite    = false,
        .updateFlags = false,
        .branchType  = BranchControl::NONE,
        .aluOp       = risc201::OperationCode::ADD
    };
    for (size_t i = 0; i < CONTROL_STORE_SIZE; ++i) {
        controlStore[i] = nop;
    }

    auto set_entry = [this](risc201::OperationCode op, bool regW, bool memR, bool memW, 
                            bool updFlags, BranchControl branch, risc201::OperationCode aluOp) {
        size_t idx = static_cast<size_t>(op);
        if (idx < CONTROL_STORE_SIZE) {
            controlStore[idx] = MicroInstruction{regW, memR, memW, updFlags, branch, aluOp};
        }
    };

    using Op = risc201::OperationCode;

    // 2. Arithmetic & Logic Operations -> Run through ALU, write back to rd
    const std::vector<Op> computeOps = {
        Op::ADD, Op::SUB, Op::MUL, Op::DIV, Op::MOD,
        Op::AND, Op::OR,  Op::XOR, Op::NOT, Op::MOV,
        Op::LSL, Op::LSR, Op::ASR, Op::ROL, Op::ROR,
        Op::NEG, Op::ABS, Op::MIN, Op::MAX
    };
    for (auto op : computeOps) {
        set_entry(op, true, false, false, false, BranchControl::NONE, op);
    }

    // 3. Compare -> Computes (rs1 - rs2), latches flags, no register write
    set_entry(Op::CMP, false, false, false, true, BranchControl::NONE, Op::CMP);

    // 4. Memory Operations -> ALU computes effective address (base + offset)
    set_entry(Op::LD, true,  true,  false, false, BranchControl::NONE, Op::ADD);
    set_entry(Op::ST, false, false, true,  false, BranchControl::NONE, Op::ADD);

    // 5. Branch Operations
    set_entry(Op::BEQ,  false, false, false, false, BranchControl::COND_EQ, Op::ADD);
    set_entry(Op::BGT,  false, false, false, false, BranchControl::COND_GT, Op::ADD);
    set_entry(Op::B,    false, false, false, false, BranchControl::UNCOND,  Op::ADD);
    set_entry(Op::CALL, true,  false, false, false, BranchControl::CALL,    Op::ADD);
    set_entry(Op::RET,  false, false, false, false, BranchControl::RET,     Op::ADD);
}