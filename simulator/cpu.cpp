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
    totalCycles = 0;
    instructionsRetired = 0;
    branchFlushes = 0;
    pc = 0;
    
    // Clear flags
    flags.IsEqual = false;
    flags.IsGreaterThan = false;
    faulted = false;
    
    // Flush/Clear pipeline latches
    if_id = IF_ID_Latch{};
    id_ex = ID_EX_Latch{};
    ex_memwb = EX_MEMWB_Latch{};

    // Flush/Clear 6-stage pipeline latches
    id_rr = ID_RR_Latch{};
    rr_ex = RR_EX_Latch{};
    ex_mem = EX_MEM_Latch{};
    mem_wb_6 = MEM_WB_Latch_6{};

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

    // 2. Arithmetic & Logic Operations
    const std::vector<Op> computeOps = {
        Op::ADD, Op::SUB, Op::MUL, Op::DIV, Op::MOD,
        Op::AND, Op::OR,  Op::XOR, Op::NOT, Op::MOV,
        Op::LSL, Op::LSR, Op::ASR, Op::ROL, Op::ROR,
        Op::NEG, Op::ABS, Op::MIN, Op::MAX
    };
    for (auto op : computeOps) {
        set_entry(op, true, false, false, false, BranchControl::NONE, op);
    }

    // 3. Compare
    set_entry(Op::CMP, false, false, false, true, BranchControl::NONE, Op::CMP);

    // 4. Memory Operations: effective address (base + offset)
    set_entry(Op::LD, true,  true,  false, false, BranchControl::NONE, Op::ADD);
    set_entry(Op::ST, false, false, true,  false, BranchControl::NONE, Op::ADD);

    // 5. Branch Operations
    set_entry(Op::BEQ,  false, false, false, false, BranchControl::COND_EQ, Op::ADD);
    set_entry(Op::BGT,  false, false, false, false, BranchControl::COND_GT, Op::ADD);
    set_entry(Op::B,    false, false, false, false, BranchControl::UNCOND,  Op::ADD);
    set_entry(Op::CALL, true,  false, false, false, BranchControl::CALL,    Op::ADD);
    set_entry(Op::RET,  false, false, false, false, BranchControl::RET,     Op::ADD);
}

// STAGE 1: INSTRUCTION FETCH (IF)
void CPU::do_IF() {
    if (pc / risc201::BYTES_PER_WORD >= instrMem.size()) {
        if_id.active = false;
        return;
    }

    if_id.instruction = instrMem[pc / risc201::BYTES_PER_WORD];
    if_id.pc = pc;
    if_id.active = true;

    pc += risc201::BYTES_PER_WORD;
}

// STAGE 2: INSTRUCTION DECODE & CONTROL ROM LOOKUP (ID)
void CPU::do_ID() {
    // If previous stage has no valid instruction, deactivate
    if (!if_id.active) {
        id_ex.active = false;
        return;
    }

    // Decode bitfields (opcode, registers, constants)
    auto decodedOpt = risc201::convertFromBinary(if_id.instruction);
    if (!decodedOpt.has_value()) {
                throw SimException(FaultType::INVALID_OPCODE,
                           "undefined opcode in instruction word " + hex32(if_id.instruction),
                           if_id.pc);
    }
    risc201::DecodedInstruction instr = decodedOpt.value();

    // Horizontal Microprogramming: Fetch control word from ROM
    size_t opIdx = static_cast<size_t>(instr.operation);
    MicroInstruction ctrlWord = (opIdx < CONTROL_STORE_SIZE) ? controlStore[opIdx] : controlStore[0];

    int32_t op1 = 0;
    int32_t op2 = 0;

   
    if (instr.operation == risc201::OperationCode::MOV || 
        instr.operation == risc201::OperationCode::NOT ||
        instr.operation == risc201::OperationCode::NEG ||
        instr.operation == risc201::OperationCode::ABS) {
        op1 = instr.usesConstantNumber ? instr.calculateFinalConstantValue() : reg[instr.sourceRegister1];
    } else {
        op1 = reg[instr.sourceRegister1];
        op2 = instr.usesConstantNumber ? instr.calculateFinalConstantValue() : reg[instr.sourceRegister2];
    }

    // Latch values into ID_EX
    id_ex.active = true;
    id_ex.pc = if_id.pc;
    id_ex.decoded = instr;
    id_ex.val1 = op1;
    id_ex.val2 = op2;
    id_ex.ctrl = ctrlWord; // Carry the control word forward to EX stage
}


// STAGE 3: EXECUTE

void CPU::do_EX() {
    
    if (!id_ex.active) {
        ex_memwb.active = false;
        return;
    }

    // Pass the inputs and the microcode's aluOp directly to the ALU
    risc201::ALUResult aluOut = alu.execute(id_ex.ctrl.aluOp, id_ex.val1, id_ex.val2);

   
    if (id_ex.ctrl.updateFlags) {
        flags.IsEqual = (id_ex.val1 == id_ex.val2);
        flags.IsGreaterThan = (id_ex.val1 > id_ex.val2);
    }

    // See if we need to take a branch
    bool takeBranch = false;
    uint32_t targetPC = 0;

    switch (id_ex.ctrl.branchType) {
        case BranchControl::NONE:
            break;
            
        case BranchControl::COND_EQ:
            if (flags.IsEqual) {
                takeBranch = true;
                targetPC = id_ex.pc + id_ex.decoded.branchJumpDistance;
            }
            break;
            
        case BranchControl::COND_GT:
            if (flags.IsGreaterThan) {
                takeBranch = true;
                targetPC = id_ex.pc + id_ex.decoded.branchJumpDistance;
            }
            break;
            
        case BranchControl::UNCOND:
            takeBranch = true;
            targetPC = id_ex.pc + id_ex.decoded.branchJumpDistance;
            break;
            
        case BranchControl::CALL:
            takeBranch = true;
            targetPC = id_ex.pc + id_ex.decoded.branchJumpDistance;
            aluOut.value = id_ex.pc + risc201::BYTES_PER_WORD;
            break;
            
        case BranchControl::RET:
            takeBranch = true;
            targetPC = static_cast<uint32_t>(reg[risc201::RETURN_ADDRESS_REG_INDEX]);
            break;
    }

    // flush
    if (takeBranch) {
        pc = targetPC;
        if_id.active = false;
        branchFlushes++; // 1 stage (IF) was flushed
    }

    // output latch
    ex_memwb.active = true;
    ex_memwb.pc = id_ex.pc;
    
    // CALL always saves to r15; everything else saves to rd
    ex_memwb.dest_reg = (id_ex.ctrl.branchType == BranchControl::CALL)
                        ? static_cast<uint8_t>(risc201::RETURN_ADDRESS_REG_INDEX)
                        : id_ex.decoded.destinationRegister;

    ex_memwb.alu_result = aluOut.value;
    
    // If store instruction, the value we want to write into RAM is sitting in rd
    ex_memwb.store_data = reg[id_ex.decoded.destinationRegister];
    
    ex_memwb.ctrl = id_ex.ctrl;
}

// Data memory access helpers
void CPU::checkWordAccess(uint32_t addr, uint32_t faultPc) const {
    if (static_cast<uint64_t>(addr) + risc201::BYTES_PER_WORD > dataMem.size()) {
        throw SimException(FaultType::OUT_OF_BOUNDS,
                           "memory access outside the 64 KB data memory", faultPc)
                  .withAddress(addr);
    }
    if (addr % risc201::BYTES_PER_WORD != 0) {
        throw SimException(FaultType::MISALIGNED,
                           "word access is not 4-byte aligned", faultPc)
                  .withAddress(addr);
    }
}

void CPU::do_ID_6() {
    if (!if_id.active) {
        id_rr.active = false;
        return;
    }

    id_rr.active = true;
    id_rr.pc = if_id.pc;
    
    // Parse instruction bitfields
    auto decodedOpt = risc201::convertFromBinary(if_id.instruction);
    if (!decodedOpt.has_value()) {
        throw SimException(FaultType::INVALID_OPCODE,
                           "undefined opcode in instruction word " + hex32(if_id.instruction),
                           if_id.pc);
    }
    id_rr.decoded = decodedOpt.value();
    
    // Look up control word from Control Store ROM array
    size_t opIdx = static_cast<size_t>(id_rr.decoded.operation);
    id_rr.ctrl = (opIdx < CONTROL_STORE_SIZE) ? controlStore[opIdx] : controlStore[0];
}

void CPU::do_RR_6() {
    if (!id_rr.active) {
        rr_ex.active = false;
        return;
    }

    rr_ex.active = true;
    rr_ex.pc = id_rr.pc;
    rr_ex.decoded = id_rr.decoded;
    rr_ex.ctrl = id_rr.ctrl;

    int32_t op1 = 0;
    int32_t op2 = 0;

    if (id_rr.decoded.operation == risc201::OperationCode::MOV || 
        id_rr.decoded.operation == risc201::OperationCode::NOT ||
        id_rr.decoded.operation == risc201::OperationCode::NEG ||
        id_rr.decoded.operation == risc201::OperationCode::ABS) {
        op1 = id_rr.decoded.usesConstantNumber ? id_rr.decoded.calculateFinalConstantValue() : reg[id_rr.decoded.sourceRegister1];
    } else {
        op1 = reg[id_rr.decoded.sourceRegister1];
        op2 = id_rr.decoded.usesConstantNumber ? id_rr.decoded.calculateFinalConstantValue() : reg[id_rr.decoded.sourceRegister2];
    }

    rr_ex.val1 = op1;
    rr_ex.val2 = op2;
}

void CPU::do_EX_6() {
    if (!rr_ex.active) {
        ex_mem.active = false;
        return;
    }

    // Step 1: Execute the ALU operation
    risc201::ALUResult aluOut = alu.execute(rr_ex.ctrl.aluOp, rr_ex.val1, rr_ex.val2);

    // Step 2: Conditional Flag Updates 
    if (rr_ex.ctrl.updateFlags) {
        flags.IsEqual = (rr_ex.val1 == rr_ex.val2);
        flags.IsGreaterThan = (rr_ex.val1 > rr_ex.val2);
    }
    // Step 3: Branch Target and Condition Evaluation
    bool takeBranch = false;
    uint32_t targetPC = 0;

    switch (rr_ex.ctrl.branchType) {
        case BranchControl::NONE:
            break;
            
        case BranchControl::COND_EQ:
            if (flags.IsEqual) {
                takeBranch = true;
                targetPC = rr_ex.pc + rr_ex.decoded.branchJumpDistance;
            }
            break;
            
        case BranchControl::COND_GT:
            if (flags.IsGreaterThan) {
                takeBranch = true;
                targetPC = rr_ex.pc + rr_ex.decoded.branchJumpDistance;
            }
            break;
            
        case BranchControl::UNCOND:
            takeBranch = true;
            targetPC = rr_ex.pc + rr_ex.decoded.branchJumpDistance;
            break;
            
        case BranchControl::CALL:
            takeBranch = true;
            targetPC = rr_ex.pc + rr_ex.decoded.branchJumpDistance;
            aluOut.value = rr_ex.pc + risc201::BYTES_PER_WORD;
            break;
            
        case BranchControl::RET:
            takeBranch = true;
            targetPC = static_cast<uint32_t>(reg[risc201::RETURN_ADDRESS_REG_INDEX]);
            break;
    }

    // Step 4: Handle branch redirection, flushes, and final latch packaging
    if (takeBranch) {
        pc = targetPC;
        if_id.active = false;
        id_rr.active = false;
        branchFlushes++;
    }

    ex_mem.active = true;
    ex_mem.pc = rr_ex.pc;
    
    ex_mem.dest_reg = (rr_ex.ctrl.branchType == BranchControl::CALL)
                        ? static_cast<uint8_t>(risc201::RETURN_ADDRESS_REG_INDEX)
                        : rr_ex.decoded.destinationRegister;

    ex_mem.alu_result = aluOut.value;
    ex_mem.store_data = reg[rr_ex.decoded.destinationRegister];
    ex_mem.ctrl = rr_ex.ctrl;
}

uint32_t CPU::readWord(uint32_t addr, uint32_t faultPc) const {
    checkWordAccess(addr, faultPc);
    // little endian
    uint32_t value = 0;
    value |= dataMem[addr];
    value |= (static_cast<uint32_t>(dataMem[addr + 1]) << 8);
    value |= (static_cast<uint32_t>(dataMem[addr + 2]) << 16);
    value |= (static_cast<uint32_t>(dataMem[addr + 3]) << 24);
    return value;
}

void CPU::writeWord(uint32_t addr, uint32_t value, uint32_t faultPc) {
    checkWordAccess(addr, faultPc);
    dataMem[addr]     = value & 0xFF;
    dataMem[addr + 1] = (value >> 8) & 0xFF;
    dataMem[addr + 2] = (value >> 16) & 0xFF;
    dataMem[addr + 3] = (value >> 24) & 0xFF;
}

// Stack pointer safeguard: sp must stay word aligned and inside [STACK_LOWEST_LIMIT, STACK_STARTING_ADDRESS]. 
//The stack is full descending, so going under the floor is an overflow and going above the start is an underflow (popping more than was pushed).
void CPU::checkStackPointer(uint32_t newSP, uint32_t faultPc) const {
    if (newSP % risc201::BYTES_PER_WORD != 0) {
        throw SimException(FaultType::MISALIGNED,
                           "stack pointer must stay 4-byte aligned", faultPc)
                  .withAddress(newSP);
    }
    if (newSP < risc201::STACK_LOWEST_LIMIT) {
        throw SimException(FaultType::STACK_OVERFLOW,
                           "stack grew past its 16 KB limit (floor is " +
                               hex32(risc201::STACK_LOWEST_LIMIT) + ")",
                           faultPc)
                  .withAddress(newSP);
    }
    if (newSP > risc201::STACK_STARTING_ADDRESS) {
        throw SimException(FaultType::STACK_UNDERFLOW,
                           "stack pointer went above the empty-stack address (" +
                               hex32(risc201::STACK_STARTING_ADDRESS) + "), more pops than pushes",
                           faultPc)
                  .withAddress(newSP);
    }
}

// Stores into the stack segment must not land below sp. 
// Everything under sp is free space in a full descending stack, 
// so a write there means a bad pointer or a skipped sp update and would corrupt later pushes.
void CPU::checkStackWrite(uint32_t addr, uint32_t faultPc) const {
    const uint32_t sp = static_cast<uint32_t>(reg[risc201::STACK_POINTER_REG_INDEX]);
    const bool inStackSegment = addr >= risc201::STACK_LOWEST_LIMIT &&
                                addr <  risc201::STACK_STARTING_ADDRESS;
    if (inStackSegment && addr < sp) {
        throw SimException(FaultType::STACK_CORRUPTION,
                           "store below the stack pointer (sp = " + hex32(sp) + ")",
                           faultPc)
                  .withAddress(addr);
    }
}

// STAGE 4: MEMORY ACCESS & WRITEBACK (MEM_WB)
void CPU::do_MEM_WB() {
    if (!ex_memwb.active) {
        return;
    }
    instructionsRetired++;
    int32_t writebackValue = ex_memwb.alu_result;
    uint32_t memAddr = static_cast<uint32_t>(ex_memwb.alu_result);

    // 1. Memory Read (LD)
    if (ex_memwb.ctrl.memRead) {
        writebackValue = static_cast<int32_t>(readWord(memAddr, ex_memwb.pc));
    }

    // 2. Memory Write (ST)
    if (ex_memwb.ctrl.memWrite) {
        checkWordAccess(memAddr, ex_memwb.pc);   // alignment/bounds first
        checkStackWrite(memAddr, ex_memwb.pc);
        writeWord(memAddr, static_cast<uint32_t>(ex_memwb.store_data), ex_memwb.pc);
    }

        if (ex_memwb.ctrl.regWrite) {
        // look at the new sp BEFORE it lands in the register, so a faulting
        // instruction never leaves sp pointing somewhere illegal
        if (ex_memwb.dest_reg == risc201::STACK_POINTER_REG_INDEX) {
            checkStackPointer(static_cast<uint32_t>(writebackValue), ex_memwb.pc);
        }
        reg[ex_memwb.dest_reg] = writebackValue;
    }
}

void CPU::do_MEM_6() {
    if (!ex_mem.active) {
        mem_wb_6.active = false;
        return;
    }

    int32_t writebackValue = ex_mem.alu_result;
    uint32_t memAddr = static_cast<uint32_t>(ex_mem.alu_result);

    // Initial output packaging for downstream writeback stage
    mem_wb_6.active = true;
    mem_wb_6.pc = ex_mem.pc;
    mem_wb_6.dest_reg = ex_mem.dest_reg;
    mem_wb_6.final_value = writebackValue;
    mem_wb_6.ctrl = ex_mem.ctrl;
}

// Clock Cycle Stepping in reverse 
void CPU::tick() {
    // a faulted CPU stays frozen until reset()
    if (faulted) return;

    try {
        totalCycles++; // 1 clock tick = 1 cycle
        do_MEM_WB();
        do_EX();
        do_ID();
        do_IF();
    } catch (const SimException&) {
        faulted = true;
        throw;   // let the CLI print the report
    }
}

void CPU::stepInstruction() {
    tick();
}
