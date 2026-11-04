#include "cpu.h"
#include "sim_exception.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

// Khushi: delivers a fault parked in a latch as a real SimException
[[noreturn]] void raisePending(const PendingFault& pf, uint32_t faultPc) {
    SimException e(pf.type, pf.msg, faultPc);
    if (pf.hasAddr) e.withAddress(pf.addr);
    throw e;
}

} // namespace

// CPU Constructor & Reset
CPU::CPU() {
    instrMem.assign(4096, 0); 
    dataMem.assign(65536, 0); 
    reset();
}

void CPU::reset() {
    alu.clearTiming(); // Keep algorithm selections; timings belong to the new run.
    for (int i = 0; i < 16; i++) {
        reg[i] = 0;
    }
    
// Khushi: sp starts at the empty-stack address 0x10000; every overflow/underflow check is relative to it
    reg[risc201::STACK_POINTER_REG_INDEX] = risc201::STACK_STARTING_ADDRESS; 
    
    totalCycles = 0;
    timeline.clear();
    instructionsRetired = 0;
    branchFlushes = 0;
    stallCycles = 0;
    forwards = 0;
    pc = 0;
    
    // Clear flags
    flags.IsEqual = false;
    flags.IsGreaterThan = false;
    faulted = false; // Khushi: 'reset' clears the fault so the CPU can run again
    halted = false;
    fetchStopped = false;
    stallRequest = false;

    // Clear microprogram trace / fault context
    curStage = "";
    curUPC = 0;
    for (auto& t : microTrace) t = StageTrace{};

    // Data memory starts zeroed on every reset (the loaded program stays in instrMem)
    std::fill(dataMem.begin(), dataMem.end(), static_cast<uint8_t>(0));
    
    // Flush/Clear pipeline latches
    if_id = IF_ID_Latch{};
    id_ex = ID_EX_Latch{};
    ex_memwb = EX_MEMWB_Latch{};

    // Flush/Clear 6-stage pipeline latches
    id_rr = ID_RR_Latch{};
    rr_ex = RR_EX_Latch{};
    ex_mem = EX_MEM_Latch{};
    mem_wb_6 = MEM_WB_Latch_6{};

    // Program BOTH control ROMs for the selected pipeline (horizontal and vertical hold the same microprogram)
    buildControlStore(mode, controlStore, dispatch);
    buildVerticalStore(mode, verticalStore, dispatch);
}

// Select 4-stage or 6-stage; always starts from a clean reset
void CPU::setMode(PipelineMode m) {
    mode = m;
    reset();
}

// Select horizontal or vertical control unit; always starts from a clean reset
void CPU::setControlUnit(ControlUnit cu) {
    controlUnit = cu;
    reset();
}

// CPU Hex Loader
// One 32-bit word per line, in hex (optional 0x prefix). Blank lines and
// '#', ';' or '//' comments are ignored. On any error nothing is loaded and
// err explains the problem ("line N: ...").
bool CPU::loadHex(const std::string& path, std::string& err) {
    err.clear();

    std::ifstream file(path);
    if (!file.is_open()) {
        err = "could not open file " + path;
        return false;
    }

    std::vector<uint32_t> words;
    std::string line;
    size_t lineNo = 0;
    const char* whitespace = " \t\r\n\f\v";

    while (std::getline(file, line)) {
        ++lineNo;

        // strip comments
        size_t cut = line.find_first_of("#;");
        if (cut != std::string::npos) line.erase(cut);
        cut = line.find("//");
        if (cut != std::string::npos) line.erase(cut);

        // trim whitespace (including a trailing '\r' from Windows line endings)
        const size_t first = line.find_first_not_of(whitespace);
        if (first == std::string::npos) continue;
        const size_t last = line.find_last_not_of(whitespace);
        line = line.substr(first, last - first + 1);

        auto fail = [&](const std::string& why) {
            err = "line " + std::to_string(lineNo) + ": " + why;
            return false;
        };

        if (words.size() >= instrMem.size()) {
            return fail("program is larger than " + std::to_string(instrMem.size()) + " words");
        }
        // must start with a hex digit (this also rejects '+' and '-' signs)
        if (!std::isxdigit(static_cast<unsigned char>(line[0]))) {
            return fail("not a hexadecimal word: '" + line + "'");
        }

        errno = 0;
        char* end = nullptr;
        const unsigned long long value = std::strtoull(line.c_str(), &end, 16);
        if (end == line.c_str() || *end != '\0') {
            return fail("not a hexadecimal word: '" + line + "'");
        }
        if (errno == ERANGE || value > 0xFFFFFFFFull) {
            return fail("value does not fit in 32 bits: '" + line + "'");
        }
        words.push_back(static_cast<uint32_t>(value));
    }

    if (words.empty()) {
        err = "file contains no instructions";
        return false;
    }

    // commit: replace the whole program, then start from a clean machine
    std::fill(instrMem.begin(), instrMem.end(), 0u);
    std::copy(words.begin(), words.end(), instrMem.begin());
    programWords = static_cast<uint32_t>(words.size());
    reset();
    return true;
}

// Microprogram sequencing helpers
// Fetch the microword at upc for the given stage: checks it exists, belongs
// to this stage and is legal, and records it in the per-stage trace.
// Control-store read. Horizontal: the ROM word is used directly.
// Vertical: the encoded word goes through the decode layer first.
MicroWord CPU::readControlStore(uint8_t upc, std::string* err) const {
    if (controlUnit == ControlUnit::VERTICAL) return decodeMicroWord(verticalStore[upc], err);
    return controlStore[upc];                          // uint8_t upc cannot exceed 255
}

MicroWord CPU::microFetch(uint8_t upc, MicroStage st, uint32_t pc) {
    curUPC = upc;
    std::string decodeErr;
    MicroWord w = readControlStore(upc, &decodeErr);
    if (!decodeErr.empty())
        throw SimException(FaultType::ILLEGAL_MICROINSTRUCTION,
                           "vertical decode error at µPC " + hex32(upc) + ": " + decodeErr, pc);
    if (!w.valid)
        throw SimException(FaultType::INVALID_MICROPC, "µPC " + hex32(upc) + " has no microword", pc);
    if (w.stage != st)
        throw SimException(FaultType::ILLEGAL_MICROINSTRUCTION,
                           "stage mismatch: microword at µPC " + hex32(upc) + " does not belong to this stage", pc);
    if (auto err = validateMicroWord(w, st); !err.empty())
        throw SimException(FaultType::ILLEGAL_MICROINSTRUCTION, err, pc);
    microTrace[static_cast<size_t>(st)] = {true, pc, upc};
    return w;
}

// Micro-sequencer: NEXT -> upc + 1, END -> routine finished. The sequencer field must
// agree with the instruction's position in the pipeline.
uint8_t CPU::advance(const MicroWord& w, uint8_t upc, bool lastStage, uint32_t pc) {
    if (lastStage != (w.seq == Seq::END))
        throw SimException(FaultType::ILLEGAL_MICROINSTRUCTION,
                           "sequencer field disagrees with pipeline position", pc);
    if (lastStage) return upc;
    if (upc == CONTROL_STORE_SIZE - 1)
        throw SimException(FaultType::INVALID_MICROPC, "µPC ran past the end of the control store", pc);
    return static_cast<uint8_t>(upc + 1);
}

// Read a microword without tracing or validating (hazard unit only)
MicroWord CPU::peek(uint8_t upc) const { return readControlStore(upc); }

// Decode + reserved-field checks. Never throws for a bad instruction: it fills pf
// so the caller can raise it now (4-stage) or park it in the latch (6-stage, where
// ID can be on a wrong path that a branch is about to flush).
risc201::DecodedInstruction CPU::decodeChecked(uint32_t word, uint32_t /*pc*/, PendingFault& pf) const {
    pf = PendingFault{};
    auto flag = [&](FaultType type, const std::string& msg) {
        pf.pending = true;
        pf.type = type;
        pf.msg = msg;
    };

    auto decodedOpt = risc201::convertFromBinary(word);
    if (!decodedOpt.has_value()) {
        // Khushi: an undefined opcode becomes an INVALID_OPCODE fault, not a crash
        flag(FaultType::INVALID_OPCODE, "undefined opcode in instruction word " + hex32(word));
        return risc201::DecodedInstruction{};
    }
    risc201::DecodedInstruction d = decodedOpt.value();

    const risc201::OperationDetails* details = risc201::findByOpcodeId(d.operation);
        if (details != nullptr && d.operation == risc201::OperationCode::FPU) {
        // floating point: register form only; bits [13:12] hold the function, bits [11:0] are reserved
        if (d.usesConstantNumber)
            flag(FaultType::ILLEGAL_INSTRUCTION, "floating-point instructions have no immediate form " + hex32(word));
        else if ((word & 0x0FFFu) != 0)
            flag(FaultType::ILLEGAL_INSTRUCTION, "reserved bits [11:0] set in instruction word " + hex32(word));
    } else if (details != nullptr && details->layoutFormat == risc201::InstructionLayout::OPERATE) {
        if (details->assemblyFormat == risc201::AssemblySyntax::NO_ARGS) {
            // nop / ret / halt: everything below the opcode is reserved
            if ((word & 0x07FFFFFFu) != 0)
                flag(FaultType::ILLEGAL_INSTRUCTION, "reserved bits set in instruction word " + hex32(word));
        } else if (d.usesConstantNumber) {
            if (static_cast<uint8_t>(d.constantModifier) > static_cast<uint8_t>(risc201::ImmediateModifier::UPPER))
                flag(FaultType::ILLEGAL_INSTRUCTION, "reserved immediate modifier in instruction word " + hex32(word));
        } else {
            // register form: bits [13:0] are unused
            if ((word & 0x3FFFu) != 0)
                flag(FaultType::ILLEGAL_INSTRUCTION, "reserved bits [13:0] set in instruction word " + hex32(word));
        }
    }
    return d;
}

// Fetch register r through the forwarding paths; false = the value is not ready yet
// (load-use) and the consumer must stall one cycle.
bool CPU::getReg(uint8_t r, int32_t& out) {
  if (mode == PipelineMode::STAGE_4) {                 // producer is one ahead, in ex_memwb
    if (ex_memwb.active && ex_memwb.dest_reg == r && peek(ex_memwb.uPC).regWrite) {
        if (peek(ex_memwb.uPC).memRead) return false;  // load-use: stall one cycle
        out = ex_memwb.alu_result; forwards++; return true; }
  } else {                                             // 6-stage
    if (ex_mem.active && ex_mem.dest_reg == r && peek(ex_mem.uPC).regWrite) {
        if (peek(ex_mem.uPC).memRead) return false;
        out = ex_mem.alu_result; forwards++; return true; }
    if (mem_wb_6.active && mem_wb_6.dest_reg == r && peek(mem_wb_6.uPC).regWrite) {
        out = mem_wb_6.final_value; forwards++; return true; }
  }
  out = reg[r]; return true;                           // WB already wrote this cycle
}

// Operand read driven by the microword's register-read bits.
// Returns false (and changes nothing) if any operand must wait for a load.
bool CPU::readOperands(const risc201::DecodedInstruction& d, const MicroWord& w,
                       int32_t& val1, int32_t& val2, int32_t& val3) {
    const uint64_t forwardsBefore = forwards;   // a stalled attempt must not count its forwards
    int32_t a = 0, b = 0, c = 0;
    bool ready = true;

    if (w.unaryOp) {
        // MOV / NOT / NEG / ABS: single operand, either the constant or rs1
        if (d.usesConstantNumber)  a = d.calculateFinalConstantValue();
        else if (w.readRs1)        ready = getReg(d.sourceRegister1, a);
    } else {
        if (w.readRs1) ready = getReg(d.sourceRegister1, a);
        if (ready && w.readRs2) {
            if (d.usesConstantNumber) b = d.calculateFinalConstantValue();
            else                      ready = getReg(d.sourceRegister2, b);
        }
    }
    // store data (rd) for ST, or the return address (r15) for RET
    if (ready && w.readRd) ready = getReg(d.destinationRegister, c);
    if (ready && w.readRa) ready = getReg(static_cast<uint8_t>(risc201::RETURN_ADDRESS_REG_INDEX), c);

    if (!ready) {
        forwards = forwardsBefore;
        return false;
    }
    val1 = a;
    val2 = b;
    val3 = c;
    return true;
}

// STAGE 1: INSTRUCTION FETCH (IF)
void CPU::do_IF() {
    // HALT has executed: stop fetching and let the pipeline drain
    if (fetchStopped) {
        if_id.active = false;
        return;
    }

    // Fetching outside the loaded program is parked as a poisoned instruction. It only
    // faults if it survives to the commit point (a wrong-path fetch is flushed first).
    if (pc % risc201::BYTES_PER_WORD != 0 ||
        pc / risc201::BYTES_PER_WORD >= programWords) {
        if_id.active = true;
        if_id.pc = pc;
        if_id.instruction = 0;
        if_id.pf = PendingFault{};
        if_id.pf.pending = true;
        if_id.pf.type = (pc % risc201::BYTES_PER_WORD != 0) ? FaultType::MISALIGNED_PC
                                                            : FaultType::PC_OUT_OF_BOUNDS;
        if_id.pf.msg = (programWords == 0) ? "no program is loaded"
                                           : "PC left the loaded program (missing HALT?)";
        if_id.pf.addr = pc;
        if_id.pf.hasAddr = true;
        return;
    }

    if_id.instruction = instrMem[pc / risc201::BYTES_PER_WORD];
    if_id.pc = pc;
    if_id.pf = PendingFault{};
    if_id.active = true;

    pc += risc201::BYTES_PER_WORD;
}

// STAGE 2: INSTRUCTION DECODE & MICROPROGRAM DISPATCH (ID)
void CPU::do_ID() {
    // If previous stage has no valid instruction, deactivate
    if (!if_id.active) {
        id_ex.active = false;
        return;
    }

    // In the 4-stage pipeline the instruction in ID is never on a wrong path
    // (a branch in EX flushes IF/ID before ID runs), so faults can be raised now.
    if (if_id.pf.pending) raisePending(if_id.pf, if_id.pc);

    PendingFault pf;
    risc201::DecodedInstruction instr = decodeChecked(if_id.instruction, if_id.pc, pf);
    if (pf.pending) raisePending(pf, if_id.pc);

    // Horizontal Microprogramming: dispatch to this opcode's microroutine, fetch its ID word
    const uint8_t upc = dispatch[static_cast<uint8_t>(instr.operation) & 0x1F];
    const MicroWord& w = microFetch(upc, MicroStage::ID, if_id.pc);

    // Register read (with forwarding); on a load-use hazard hold IF/ID and send a bubble
    int32_t op1 = 0, op2 = 0, op3 = 0;
    if (!readOperands(instr, w, op1, op2, op3)) {
        stallRequest = true;
        id_ex.active = false;
        return;
    }

    // Latch values into ID_EX
    id_ex.active = true;
    id_ex.pc = if_id.pc;
    id_ex.decoded = instr;
    id_ex.val1 = op1;
    id_ex.val2 = op2;
    id_ex.val3 = op3;
    id_ex.uPC = advance(w, upc, false, if_id.pc);   // micro-address of the EX word
}


// STAGE 3: EXECUTE

void CPU::do_EX() {
    
    if (!id_ex.active) {
        ex_memwb.active = false;
        return;
    }

    ExOut out = execCore(id_ex.decoded, id_ex.pc, id_ex.uPC, id_ex.val1, id_ex.val2, id_ex.val3);

    // flush
    if (out.take) {
        pc = out.target;
        if_id.active = false;
        branchFlushes++; // 1 stage (IF) was flushed
    }
    // HALT executed: discard everything younger
    if (fetchStopped) {
        if_id.active = false;
    }

    // output latch
    ex_memwb.active = true;
    ex_memwb.pc = id_ex.pc;
    ex_memwb.uPC = out.nextUPC;
    ex_memwb.dest_reg = out.dest;
    
    // Khushi: pass the base register on, so memCore can tell a pop (load through sp) from other loads
    ex_memwb.base_reg = id_ex.decoded.sourceRegister1;
    
    ex_memwb.alu_result = out.value;
    
    // If store instruction, the value we want to write into RAM was read as val3 (rd)
    ex_memwb.store_data = id_ex.val3;
}

// Shared execute core for both pipelines: fetches the EX microword and performs
// ALU work, traps, flag update, branch resolution and link/halt handling.
ExOut CPU::execCore(const risc201::DecodedInstruction& d, uint32_t pc, uint8_t uPC,
                    int32_t a, int32_t b, int32_t c) {
    const MicroWord& w = microFetch(uPC, MicroStage::EX, pc);

    ExOut out;
    out.nextUPC = advance(w, uPC, false, pc);

    // Arithmetic traps
    if (w.trapDivZero && b == 0) {
        throw SimException(FaultType::DIV_BY_ZERO, "division by zero", pc);
    }
    if (w.aluEnable) {
        risc201::ALUResult aluOut = alu.execute(w.aluOp, a, b, d.fpuFunction);
        if (w.trapOverflow && aluOut.flags.overflow) {
            throw SimException(FaultType::ARITHMETIC_OVERFLOW, "arithmetic overflow", pc);
        }
        out.value = aluOut.value;
    }

    if (w.updateFlags) {
        flags = risc201::evaluateComparison(a, b);
    }

    // See if we need to take a branch
    switch (w.branch) {
        case BranchControl::NONE:
            break;

        case BranchControl::COND_EQ:
            if (flags.IsEqual) {
                out.take = true;
                out.target = pc + static_cast<uint32_t>(d.branchJumpDistance);
            }
            break;

        case BranchControl::COND_GT:
            if (flags.IsGreaterThan) {
                out.take = true;
                out.target = pc + static_cast<uint32_t>(d.branchJumpDistance);
            }
            break;

        case BranchControl::UNCOND:
        case BranchControl::CALL:
            out.take = true;
            out.target = pc + static_cast<uint32_t>(d.branchJumpDistance);
            break;

        case BranchControl::RET:
            out.take = true;
            out.target = static_cast<uint32_t>(c);       // return address (r15) read as val3
            break;
    }

    // A redirect must land on a word-aligned address inside the loaded program
    if (out.take) {
        if (out.target % risc201::BYTES_PER_WORD != 0) {
            throw SimException(FaultType::MISALIGNED_PC,
                               "branch target is not 4-byte aligned", pc)
                      .withAddress(out.target);
        }
        if (out.target / risc201::BYTES_PER_WORD >= programWords) {
            throw SimException(FaultType::PC_OUT_OF_BOUNDS,
                               "branch target is outside the loaded program", pc)
                      .withAddress(out.target);
        }
    }

    // CALL saves PC+4 and writes it to r15; everything else writes rd
    if (w.linkPC) out.value = static_cast<int32_t>(pc + risc201::BYTES_PER_WORD);
    out.dest = w.destIsRA ? static_cast<uint8_t>(risc201::RETURN_ADDRESS_REG_INDEX)
                          : d.destinationRegister;

    // HALT: stop fetching; the pipeline drains and HALT stops the CPU at writeback
    if (w.stopFetch) fetchStopped = true;

    return out;
}

// Khushi: memory protection: every load/store passes this check for the 64 KB bounds
// and 4-byte alignment
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
    id_rr.uPC = 0;
    id_rr.decoded = risc201::DecodedInstruction{};
    id_rr.pf = PendingFault{};

    // A fault found here may be on a wrong path (the branch ahead resolves one cycle
    // later in EX), so it is parked in the latch and only raised in RR.
    if (if_id.pf.pending) {
        id_rr.pf = if_id.pf;
        return;
    }

    PendingFault pf;
    id_rr.decoded = decodeChecked(if_id.instruction, if_id.pc, pf);
    if (pf.pending) {
        id_rr.pf = pf;
        return;
    }

    // Look up this opcode's microroutine in the Control Store ROM and fetch its ID word
    try {
        const uint8_t upc = dispatch[static_cast<uint8_t>(id_rr.decoded.operation) & 0x1F];
        const MicroWord& w = microFetch(upc, MicroStage::ID, if_id.pc);
        id_rr.uPC = advance(w, upc, false, if_id.pc);   // micro-address of the RR word
    } catch (const SimException& e) {
        id_rr.pf.pending = true;
        id_rr.pf.type = e.type();
        id_rr.pf.msg = e.what();
    }
}

void CPU::do_RR_6() {
    if (!id_rr.active) {
        rr_ex.active = false;
        return;
    }

    // RR is never on a wrong path (a branch in EX flushes ID/RR first), so raise now
    if (id_rr.pf.pending) raisePending(id_rr.pf, id_rr.pc);

    const MicroWord& w = microFetch(id_rr.uPC, MicroStage::RR, id_rr.pc);

    // Register read (with forwarding); on a load-use hazard hold ID/RR and send a bubble
    int32_t op1 = 0, op2 = 0, op3 = 0;
    if (!readOperands(id_rr.decoded, w, op1, op2, op3)) {
        stallRequest = true;
        rr_ex.active = false;
        return;
    }

    rr_ex.active = true;
    rr_ex.pc = id_rr.pc;
    rr_ex.decoded = id_rr.decoded;
    rr_ex.val1 = op1;
    rr_ex.val2 = op2;
    rr_ex.val3 = op3;
    rr_ex.uPC = advance(w, id_rr.uPC, false, id_rr.pc);   // micro-address of the EX word
}

void CPU::do_EX_6() {
    if (!rr_ex.active) {
        ex_mem.active = false;
        return;
    }

    ExOut out = execCore(rr_ex.decoded, rr_ex.pc, rr_ex.uPC, rr_ex.val1, rr_ex.val2, rr_ex.val3);

    // Handle branch redirection and flushes
    if (out.take) {
        pc = out.target;
        if_id.active = false;
        id_rr.active = false;
        branchFlushes++;
    }
    // HALT executed: discard everything younger
    if (fetchStopped) {
        if_id.active = false;
        id_rr.active = false;
    }

    ex_mem.active = true;
    ex_mem.pc = rr_ex.pc;
    ex_mem.uPC = out.nextUPC;
    ex_mem.dest_reg = out.dest;

    // Khushi: pass the base register on, so memCore can tell a pop (load through sp) from other loads
    ex_mem.base_reg = rr_ex.decoded.sourceRegister1;
    
    ex_mem.alu_result = out.value;
    ex_mem.store_data = rr_ex.val3;
}

// Khushi: checked 32-bit load, little endian; a bad address faults here 
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

// Khushi: checked 32-bit store, little endian; a bad address faults here
void CPU::writeWord(uint32_t addr, uint32_t value, uint32_t faultPc) {
    checkWordAccess(addr, faultPc);
    dataMem[addr]     = value & 0xFF;
    dataMem[addr + 1] = (value >> 8) & 0xFF;
    dataMem[addr + 2] = (value >> 16) & 0xFF;
    dataMem[addr + 3] = (value >> 24) & 0xFF;
}

// Khushi: stack boundary safety check on sp (alignment, overflow, underflow); it runs before sp is written back
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

// Khushi: stack corruption guard: a store into the stack area below sp raises STACK_CORRUPTION
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

// Khushi: all LD/ST protection lives here, so the 4-stage and 6-stage pipelines share it
// Returns the value that continues to writeback (loaded word, or the ALU result).
int32_t CPU::memCore(const MicroWord& w, uint32_t pc, uint8_t baseReg,
                     int32_t aluResult, int32_t storeData) {
    int32_t value = aluResult;
    const uint32_t memAddr = static_cast<uint32_t>(aluResult);

    // 1. Memory Read (LD)
    if (w.memRead) {
        
        // Khushi: pop on an empty stack: load through sp at/above 0x10000 raises STACK_UNDERFLOW
        if (baseReg == risc201::STACK_POINTER_REG_INDEX &&
            memAddr >= risc201::STACK_STARTING_ADDRESS) {
            throw SimException(FaultType::STACK_UNDERFLOW,
                               "load from at/above the empty-stack address (" +
                                   hex32(risc201::STACK_STARTING_ADDRESS) + "), pop on an empty stack",
                               pc)
                      .withAddress(memAddr);
        }
        value = static_cast<int32_t>(readWord(memAddr, pc));
    }

    // 2. Memory Write (ST)
    if (w.memWrite) {
         // Khushi: ST protection: bounds/alignment first, then the below-sp stack guard
        checkWordAccess(memAddr, pc);
        
        checkStackWrite(memAddr, pc);
        writeWord(memAddr, static_cast<uint32_t>(storeData), pc);
    }
    return value;
}

// Khushi: writeback core: sp is checked before the register write 
void CPU::wbCore(const MicroWord& w, uint32_t pc, uint8_t destReg, int32_t value) {
    if (w.regWrite) {
        // Khushi: a bad sp is never stored; the fault fires first and sp keeps its old value
        if (destReg == risc201::STACK_POINTER_REG_INDEX) {
            checkStackPointer(static_cast<uint32_t>(value), pc);
        }
        reg[destReg] = value;
    }
    // counted only once everything above succeeded
    instructionsRetired++;
    if (w.halt) halted = true;
}

// STAGE 4: MEMORY ACCESS & WRITEBACK (MEM_WB)
void CPU::do_MEM_WB() {
    if (!ex_memwb.active) {
        return;
    }

    const MicroWord& w = microFetch(ex_memwb.uPC, MicroStage::MEMWB, ex_memwb.pc);
    advance(w, ex_memwb.uPC, true, ex_memwb.pc);   // last word of the routine must carry END

    const int32_t value = memCore(w, ex_memwb.pc, ex_memwb.base_reg,
                                  ex_memwb.alu_result, ex_memwb.store_data);
    wbCore(w, ex_memwb.pc, ex_memwb.dest_reg, value);
}

void CPU::do_MEM_6() {
    if (!ex_mem.active) {
        mem_wb_6.active = false;
        return;
    }

    const MicroWord& w = microFetch(ex_mem.uPC, MicroStage::MEM, ex_mem.pc);
    const uint8_t nextUPC = advance(w, ex_mem.uPC, false, ex_mem.pc);

    const int32_t value = memCore(w, ex_mem.pc, ex_mem.base_reg,
                                  ex_mem.alu_result, ex_mem.store_data);

    // Output packaging for downstream writeback stage
    mem_wb_6.active = true;
    mem_wb_6.pc = ex_mem.pc;
    mem_wb_6.uPC = nextUPC;
    mem_wb_6.dest_reg = ex_mem.dest_reg;
    mem_wb_6.final_value = value;
}

void CPU::do_WB_6() {
    if (!mem_wb_6.active) {
        return;
    }

    const MicroWord& w = microFetch(mem_wb_6.uPC, MicroStage::WB, mem_wb_6.pc);
    advance(w, mem_wb_6.uPC, true, mem_wb_6.pc);   // last word of the routine must carry END

    wbCore(w, mem_wb_6.pc, mem_wb_6.dest_reg, mem_wb_6.final_value);
}

// Clock Cycle Stepping in reverse 
void CPU::tick() {
    
    // Khushi: a faulted CPU stays frozen until reset
    if (faulted || halted) return;

    for (auto& t : microTrace) t = StageTrace{};
    stallRequest = false;

    // Timeline: remember what sits in every stage's INPUT latch before the stages run
    const bool rec = recordTimeline && timeline.size() < TIMELINE_CAP;
    InputSnapshot pre;
    if (rec) pre = snapshotInputs();
    const uint64_t flushesBefore = branchFlushes;
    const bool fetchStoppedBefore = fetchStopped;

    try {
        totalCycles++; // 1 clock tick = 1 cycle

        if (mode == PipelineMode::STAGE_6) {
            // Reverse-order stage evaluation for 6-stage pipeline
            curStage = "WB";  do_WB_6();
            curStage = "MEM"; do_MEM_6();
            curStage = "EX";  do_EX_6();
            curStage = "RR";  do_RR_6();
            // a load-use stall in RR holds ID and IF (and their latches) for this cycle
            if (!stallRequest) {
                curStage = "ID"; do_ID_6();
                curStage = "IF"; do_IF();
            }
        } else {
            // Existing 4-stage pipeline execution path
            curStage = "MEMWB"; do_MEM_WB();
            curStage = "EX";    do_EX();
            curStage = "ID";    do_ID();
            // a load-use stall in ID holds IF (and the IF/ID latch) for this cycle
            if (!stallRequest) {
                curStage = "IF"; do_IF();
            }
        }

        if (stallRequest) stallCycles++;

        // a taken branch or a HALT in EX squashed the younger instructions this cycle
        if (rec) recordCycle(pre, branchFlushes > flushesBefore || (fetchStopped && !fetchStoppedBefore));
        
    // Khushi: on any fault, freeze the CPU, attach the stage/cycle/microPC, and rethrow
    // so the CLI can print the report
    } catch (SimException& e) {
        faulted = true;
        e.setContext(curStage, totalCycles, curUPC,
                     mode == PipelineMode::STAGE_6 ? "6-stage" : "4-stage");
        throw;   
    }
}


// Ticks until n more instructions retire, or the CPU halts or faults.
// Returns how many instructions actually retired. A fault propagates as SimException.
uint64_t CPU::runUntilRetire(uint64_t n) {
    const uint64_t start = instructionsRetired;
    const uint64_t target = start + n;
    uint64_t guard = 0;
    const uint64_t cycleCap = n * 1000 + 1000;   // safety net against a program that never retires

    while (instructionsRetired < target && !halted && !faulted && guard++ < cycleCap) {
        tick();
    }
    return instructionsRetired - start;
}


// ---------------------------------------------------------------------------
// Timeline recorder. It only OBSERVES the pipeline; it never changes any state.
// Stages are evaluated back-to-front, so each stage works on what its input latch held
// BEFORE the tick. Two exceptions: IF fetches into if_id (read after the tick), and a
// flush clears if_id / id_rr before ID / RR get to run (those cells are marked 'x').
// ---------------------------------------------------------------------------
CPU::InputSnapshot CPU::snapshotInputs() const {
    InputSnapshot s;
    s.active[1] = if_id.active;  s.pc[1] = if_id.pc;
    if (mode == PipelineMode::STAGE_6) {
        s.active[2] = id_rr.active;    s.pc[2] = id_rr.pc;
        s.active[3] = rr_ex.active;    s.pc[3] = rr_ex.pc;
        s.active[4] = ex_mem.active;   s.pc[4] = ex_mem.pc;
        s.active[5] = mem_wb_6.active; s.pc[5] = mem_wb_6.pc;
    } else {
        s.active[3] = id_ex.active;    s.pc[3] = id_ex.pc;
        s.active[4] = ex_memwb.active; s.pc[4] = ex_memwb.pc;
    }
    return s;
}

void CPU::recordCycle(const InputSnapshot& pre, bool flushed) {
    CycleRecord row;
    row.cycle = totalCycles;

    auto put = [&](int slot, char tag) {
        row.cell[slot].valid = true;
        row.cell[slot].pc = pre.pc[slot];
        row.cell[slot].tag = tag;
    };
    // ID (and RR in the 6-stage pipe) can be squashed, or held by a load-use stall
    const char frontTag = flushed ? 'x' : (stallRequest ? 'S' : ' ');

    if (pre.active[1]) put(1, frontTag);
    if (pre.active[3]) put(3, ' ');
    if (pre.active[4]) put(4, ' ');
    if (mode == PipelineMode::STAGE_6) {
        if (pre.active[2]) put(2, frontTag);
        if (pre.active[5]) put(5, ' ');
    }
    // IF only ran if nothing stalled the front end and fetching has not been stopped by HALT
    if (!stallRequest && if_id.active) {
        row.cell[0].valid = true;
        row.cell[0].pc = if_id.pc;
    }
    timeline.push_back(row);
}

uint32_t CPU::nextExecPC() const {
    if (mode == PipelineMode::STAGE_6) {
        return rr_ex.pc; // Instruction about to enter EX in 6-stage
    } else {
        return id_ex.pc; // Instruction about to enter EX in 4-stage
    }
}

// ASCII Stack Memory Visualizer
void CPU::printStackMemory(std::ostream& out, int maxWords) const {
    const uint32_t sp =
        static_cast<uint32_t>(reg[risc201::STACK_POINTER_REG_INDEX]);

    const uint32_t base = risc201::STACK_STARTING_ADDRESS;
    const uint32_t limit = risc201::STACK_LOWEST_LIMIT;

    if (maxWords < 1) {
        maxWords = 16;
    }

    out << "\n========== RISC201 STACK ==========\n";
    out << "PC         : " << hex32(pc) << '\n';
    out << "last µPC   : " << hex32(curUPC) << '\n';
    out << "SP         : " << hex32(sp) << '\n';
    out << "Stack base : " << hex32(base) << '\n';
    out << "Stack limit: " << hex32(limit) << '\n';
    out << "-----------------------------------\n";
    out << "Address      | Contents   | Status\n";
    out << "-----------------------------------\n";

    if (sp == base && sp >= limit) {
        out << "  Stack is empty.\n";
        out << "===================================\n";
        return;
    }

    if (sp < limit || sp > base ||
        sp % risc201::BYTES_PER_WORD != 0) {
        out << "  Invalid stack pointer; cannot render frame.\n";
        out << "===================================\n";
        return;
    }

    // Start a few words BELOW the stack pointer to show "Free" space
    uint32_t startAddr = sp - (4 * risc201::BYTES_PER_WORD);
    int shown = 0;

    for (uint32_t addr = startAddr;
         addr < base && shown < maxWords;
         addr += risc201::BYTES_PER_WORD, ++shown) {
        try {
            uint32_t value = readWord(addr, pc);

            // Determine the marker string
            std::string marker;
            if (addr < sp) {
                marker = "FREE";
            } else if (addr == sp) {
                marker = "<- SP (TOP)";
            } else if (addr == base - risc201::BYTES_PER_WORD) {
                marker = "USED (Return Address / Base)"; 
            } else {
                marker = "USED";
            }

            out << "  " << hex32(addr)
                << " | " << hex32(value)
                << " | " << marker
                << '\n';
                
        } catch (const SimException&) {
            out << "  " << hex32(addr)
                << " | <error>    | UNREADABLE\n";
        }
    }

    if (shown == maxWords &&
        static_cast<uint64_t>(startAddr) +
            static_cast<uint64_t>(shown) * risc201::BYTES_PER_WORD < base) {
        out << "  ... more stack words omitted\n";
    }

    out << "===================================\n";
}
