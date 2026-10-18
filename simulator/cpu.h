#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <iosfwd>
#include "alu.h"
#include "isa.h"
#include "sim_exception.h"
#include "horizontal_microcode.h"

// POISON RECORD: a fault found while decoding/fetching on a possibly wrong path.
// It travels with the instruction and is only raised if the instruction survives
// to the commit point (a flushed wrong-path instruction never faults).
struct PendingFault {
    bool pending = false;
    FaultType type = FaultType::GENERIC;
    std::string msg;
    uint32_t addr = 0;
    bool hasAddr = false;
};

// PIPELINE LATCHES to store intermediate values at each stage
struct IF_ID_Latch {
    bool active = false; 
    uint32_t pc = 0;
    uint32_t instruction = 0;
    PendingFault pf;                     // deferred fetch/decode fault (poison)
};

struct ID_EX_Latch {
    bool active = false; 
    uint32_t pc = 0;
    uint8_t uPC = 0;                     // micro-address of this instruction's next microword
    
    risc201::DecodedInstruction decoded; // Carries rd, rs1, rs2, constants
    int32_t val1 = 0;                    // Read from register file (rs1)
    int32_t val2 = 0;                    // Read from register file (rs2) OR immediate
    int32_t val3 = 0;                    // Store data (rd) for ST, or return address (r15) for RET
};

struct EX_MEMWB_Latch {
    bool active = false; 
    uint32_t pc = 0;            
    uint8_t uPC = 0;                     // micro-address of this instruction's next microword
    uint8_t dest_reg = 0;
    uint8_t base_reg = 0;                // rs1 of the instruction (to detect loads/stores through sp)
    int32_t alu_result = 0;
    int32_t store_data = 0;              // Raw value of rs1 to store (for ST)
};

// 6-Stage Pipeline Latches
struct ID_RR_Latch {
    bool active = false;
    uint32_t pc = 0;
    uint8_t uPC = 0;                     // micro-address of this instruction's next microword
    risc201::DecodedInstruction decoded;
    PendingFault pf;                     // deferred decode fault (poison)
};

struct RR_EX_Latch {
    bool active = false;
    uint32_t pc = 0;
    uint8_t uPC = 0;                     // micro-address of this instruction's next microword
    risc201::DecodedInstruction decoded;
    int32_t val1 = 0;
    int32_t val2 = 0;
    int32_t val3 = 0;                    // Store data (rd) for ST, or return address (r15) for RET
};

struct EX_MEM_Latch {
    bool active = false;
    uint32_t pc = 0;
    uint8_t uPC = 0;                     // micro-address of this instruction's next microword
    uint8_t dest_reg = 0;
    uint8_t base_reg = 0;                // rs1 of the instruction (to detect loads/stores through sp)
    int32_t alu_result = 0;
    int32_t store_data = 0;
};

struct MEM_WB_Latch_6 {
    bool active = false;
    uint32_t pc = 0;
    uint8_t uPC = 0;                     // micro-address of this instruction's next microword
    uint8_t dest_reg = 0;
    int32_t final_value = 0;
};

enum class PipelineMode {
    STAGE_4,
    STAGE_6
};

// RESULT of the shared execute core (used by both the 4-stage and 6-stage EX stages)
struct ExOut {
    int32_t  value = 0;                  // ALU result (or return address PC+4 for CALL)
    uint8_t  dest = 0;                   // destination register (r15 for CALL)
    bool     take = false;               // true if a branch/call/ret redirects the PC
    uint32_t target = 0;                 // branch target when take == true
    uint8_t  nextUPC = 0;                // micro-address for the next stage
};

// Per-stage microprogram trace (what each stage executed this cycle, for the CLI)
struct StageTrace {
    bool active = false;
    uint32_t pc = 0;
    uint8_t uPC = 0;
};

// PIPELINED CPU CLASS
class CPU {
public:
    // One trace slot per MicroStage value (IF, ID, RR, EX, MEM, WB, MEMWB)
    static constexpr size_t MICRO_TRACE_SLOTS = 7;

    std::vector<uint32_t> instrMem; // 4096 words (16 KB)
    std::vector<uint8_t>  dataMem;  // 65536 bytes (64 KB)
    
    // Processor state
    int32_t reg[16]; 
    uint32_t pc;
    risc201::ProcessorFlags flags;
    bool faulted = false;   // set when a hardware fault stops the CPU
    bool halted = false;    // set when a HALT instruction retires
    bool fetchStopped = false;  // set when HALT executes: stop fetching, let the pipeline drain
    bool stallRequest = false;  // set by a stage that needs the front of the pipe to hold this cycle
    uint32_t programWords = 0;  // number of instruction words loaded by loadHex

    // Pipeline Registers
    IF_ID_Latch if_id;
    ID_EX_Latch id_ex;
    EX_MEMWB_Latch ex_memwb;

    // 6-Stage Pipeline Registers
    ID_RR_Latch id_rr;
    RR_EX_Latch rr_ex;
    EX_MEM_Latch ex_mem;
    MEM_WB_Latch_6 mem_wb_6;
    
    // Microprogrammed Control Store
    MicroWord controlStore[CONTROL_STORE_SIZE];
    uint8_t dispatch[32] = {};      // opcode -> entry micro-address of that opcode's routine
    
    risc201::ALU alu; // Isolated ALU class

    PipelineMode mode = PipelineMode::STAGE_4;
    uint64_t totalCycles = 0;
    uint64_t instructionsRetired = 0;
    uint64_t branchFlushes = 0;
    uint64_t stallCycles = 0;       // cycles lost to load-use stalls
    uint64_t forwards = 0;          // operands supplied by the forwarding paths

    // Microprogram trace and fault context
    const char* curStage = "";      // name of the stage currently executing (for fault reports)
    uint8_t curUPC = 0;             // micro-address most recently fetched (for fault reports)
    StageTrace microTrace[MICRO_TRACE_SLOTS];
    bool traceMicro = false;        // when true, the CLI prints the micro state after every tick

    uint32_t nextExecPC() const;

    CPU(); 

    void reset();
    bool loadHex(const std::string& path, std::string& err);
    void loadControlStore(); // Maps opcodes to microinstructions at startup
    void setMode(PipelineMode m);   // selects 4-stage or 6-stage, then resets the CPU
    
    void tick(); 
    void stepInstruction();  // Wrapper that calls tick() to stay compatible with main.cpp
    uint64_t runUntilRetire(uint64_t n);  // ticks until n more instructions retire (or halt/fault); returns how many retired

    // Microprogram sequencing helpers
    const MicroWord& microFetch(uint8_t upc, MicroStage st, uint32_t pc);   // fetch + validate + trace
    const MicroWord& peek(uint8_t upc) const;                               // read a microword without side effects (hazard unit)
    uint8_t advance(const MicroWord& w, uint8_t upc, bool lastStage, uint32_t pc);  // sequencer: next micro-address

    // Decode and operand read
    risc201::DecodedInstruction decodeChecked(uint32_t word, uint32_t pc, PendingFault& pf) const;  // decode + reserved-field checks; fills pf instead of throwing
    bool getReg(uint8_t r, int32_t& out);   // read register r through the forwarding paths; false = must stall
    bool readOperands(const risc201::DecodedInstruction& d, const MicroWord& w,
                      int32_t& val1, int32_t& val2, int32_t& val3);  // false = must stall

    // Shared stage cores (used by both pipelines)
    ExOut   execCore(const risc201::DecodedInstruction& d, uint32_t pc, uint8_t uPC,
                     int32_t a, int32_t b, int32_t c);
    int32_t memCore(const MicroWord& w, uint32_t pc, uint8_t baseReg, int32_t aluResult, int32_t storeData);
    void    wbCore(const MicroWord& w, uint32_t pc, uint8_t destReg, int32_t value);

    // Checked data memory access (throws SimException on a bad address)
    void checkWordAccess(uint32_t addr, uint32_t faultPc) const;
    uint32_t readWord(uint32_t addr, uint32_t faultPc) const;
    void writeWord(uint32_t addr, uint32_t value, uint32_t faultPc);
    
    // Stack safety checks (throw SimException when something is wrong)
    void checkStackPointer(uint32_t newSP, uint32_t faultPc) const;
    // ASCII stack memory visualizer
    void printStackMemory(std::ostream& out, int maxWords = 16) const;

    void checkStackWrite(uint32_t addr, uint32_t faultPc) const;

    // Pipeline stages 
    void do_MEM_WB();
    void do_EX();
    void do_ID();
    void do_IF();
    // 6-Stage Pipeline Stages
    void do_ID_6();
    void do_RR_6();
    void do_EX_6();
    void do_MEM_6();
    void do_WB_6();
};
