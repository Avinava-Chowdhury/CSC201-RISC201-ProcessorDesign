#include "vertical_microcode.h"
#include "cpu.h"            // PipelineMode
#include <cstdio>
#include <stdexcept>

using namespace risc201;

namespace {

enum : unsigned { RD_NONE = 0, RD_RS1 = 1, RD_RS1_RS2 = 2, RD_RS1_UN = 3, RD_STORE = 4, RD_RA = 5 };
enum : unsigned { FLG_NONE = 0, FLG_UPDATE = 1, FLG_TRAP_DIV = 2 };
enum : unsigned { LNK_NONE = 0, LNK_CALL = 1, LNK_STOP = 2 };
enum : unsigned { MS_NONE = 0, MS_REGWRITE = 1, MS_LOAD = 2, MS_STORE = 3, MS_HALT = 4 };

constexpr unsigned STAGE_INVALID = 7;

[[noreturn]] void unrepresentable(const char* why) {
    throw std::logic_error(std::string("vertical encode: ") + why);
}

unsigned encodeReads(const MicroWord& w) {
    const bool a = w.readRs1, b = w.readRs2, d = w.readRd, r = w.readRa, u = w.unaryOp;
    if (!a && !b && !d && !r && !u) return RD_NONE;
    if ( a && !b && !d && !r && !u) return RD_RS1;
    if ( a &&  b && !d && !r && !u) return RD_RS1_RS2;
    if ( a && !b && !d && !r &&  u) return RD_RS1_UN;
    if ( a &&  b &&  d && !r && !u) return RD_STORE;
    if (!a && !b && !d &&  r && !u) return RD_RA;
    unrepresentable("register-read combination has no RDSEL code");
}

unsigned encodeMem(const MicroWord& w) {
    const bool mr = w.memRead, mw = w.memWrite, rw = w.regWrite, h = w.halt;
    if (!mr && !mw && !rw && !h) return MS_NONE;
    if (!mr && !mw &&  rw && !h) return MS_REGWRITE;
    if ( mr && !mw &&  rw && !h) return MS_LOAD;
    if (!mr &&  mw && !rw && !h) return MS_STORE;
    if (!mr && !mw && !rw &&  h) return MS_HALT;
    unrepresentable("memory/writeback combination has no MSEL code");
}

} // namespace

VMicroWord encodeMicroWord(const MicroWord& w) {
    if (!w.valid) return VROM_EMPTY;

    unsigned payload = 0;
    switch (w.stage) {
        case MicroStage::IF:
            break;
        case MicroStage::ID:
        case MicroStage::RR:
            payload = encodeReads(w);
            break;
        case MicroStage::EX: {
            unsigned alu;
            if (w.aluEnable) {
                if (w.aluOp == OperationCode::NOP) unrepresentable("aluEnable with aluOp NOP");
                alu = static_cast<unsigned>(w.aluOp);
            } else {
                if (w.aluOp != OperationCode::NOP) unrepresentable("aluOp set but ALU disabled");
                alu = static_cast<unsigned>(OperationCode::NOP);
            }
            unsigned flg;
            if (w.updateFlags && !w.trapDivZero && !w.trapOverflow) flg = FLG_UPDATE;
            else if (!w.updateFlags && w.trapDivZero && w.trapOverflow) flg = FLG_TRAP_DIV;
            else if (!w.updateFlags && !w.trapDivZero && !w.trapOverflow) flg = FLG_NONE;
            else unrepresentable("flag/trap combination has no FLG code");
            unsigned lnk;
            if (w.linkPC && w.destIsRA && !w.stopFetch) lnk = LNK_CALL;
            else if (!w.linkPC && !w.destIsRA && w.stopFetch) lnk = LNK_STOP;
            else if (!w.linkPC && !w.destIsRA && !w.stopFetch) lnk = LNK_NONE;
            else unrepresentable("link/stop combination has no LNK code");
            payload = (alu << 7) | (static_cast<unsigned>(w.branch) << 4) | (flg << 2) | lnk;
            break;
        }
        case MicroStage::MEM:
        case MicroStage::WB:
        case MicroStage::MEMWB:
            payload = encodeMem(w);
            break;
    }
    return static_cast<VMicroWord>((static_cast<unsigned>(w.stage) << 13) |
                                   ((w.seq == Seq::END ? 1u : 0u) << 12) | payload);
}

MicroWord decodeMicroWord(VMicroWord v, std::string* err) {
    MicroWord w;                                   // everything off, valid == false
    const unsigned stage = (v >> 13) & 0x7;
    const unsigned payload = v & 0x0FFF;
    auto bad = [&](const std::string& why) { w = MicroWord{}; if (err) *err = why; return w; };

    if (stage == STAGE_INVALID) return w;          // empty ROM location

    w.valid = true;
    w.stage = static_cast<MicroStage>(stage);
    w.seq   = ((v >> 12) & 1) ? Seq::END : Seq::NEXT;

    switch (w.stage) {
        case MicroStage::IF:
            if (payload) return bad("IF word has a non-zero payload");
            break;
        case MicroStage::ID:
        case MicroStage::RR:
            if (payload & ~0x7u) return bad("read word has bits set above RDSEL");
            switch (payload & 0x7) {
                case RD_NONE: break;
                case RD_RS1:     w.readRs1 = true; break;
                case RD_RS1_RS2: w.readRs1 = w.readRs2 = true; break;
                case RD_RS1_UN:  w.readRs1 = true; w.unaryOp = true; break;
                case RD_STORE:   w.readRs1 = w.readRs2 = w.readRd = true; break;
                case RD_RA:      w.readRa = true; break;
                default: return bad("undefined RDSEL code " + std::to_string(payload & 0x7));
            }
            break;
        case MicroStage::EX: {
            const unsigned alu = (payload >> 7) & 0x1F, br = (payload >> 4) & 0x7,
                           flg = (payload >> 2) & 0x3,  lnk = payload & 0x3;
            if (!findByOpcodeId(static_cast<uint8_t>(alu)))   return bad("undefined ALU code " + std::to_string(alu));
            if (br > static_cast<unsigned>(BranchControl::RET)) return bad("undefined BR code " + std::to_string(br));
            w.aluOp = static_cast<OperationCode>(alu);
            w.aluEnable = (w.aluOp != OperationCode::NOP);
            w.branch = static_cast<BranchControl>(br);
            switch (flg) {
                case FLG_NONE: break;
                case FLG_UPDATE: w.updateFlags = true; break;
                case FLG_TRAP_DIV: w.trapDivZero = w.trapOverflow = true; break;
                default: return bad("undefined FLG code 3");
            }
            switch (lnk) {
                case LNK_NONE: break;
                case LNK_CALL: w.linkPC = w.destIsRA = true; break;
                case LNK_STOP: w.stopFetch = true; break;
                default: return bad("undefined LNK code 3");
            }
            break;
        }
        case MicroStage::MEM:
        case MicroStage::WB:
        case MicroStage::MEMWB:
            if (payload & ~0x7u) return bad("memory word has bits set above MSEL");
            switch (payload & 0x7) {
                case MS_NONE: break;
                case MS_REGWRITE: w.regWrite = true; break;
                case MS_LOAD:     w.memRead = w.regWrite = true; break;
                case MS_STORE:    w.memWrite = true; break;
                case MS_HALT:     w.halt = true; break;
                default: return bad("undefined MSEL code " + std::to_string(payload & 0x7));
            }
            break;
    }
    return w;
}

void buildVerticalStore(PipelineMode mode, VMicroWord rom[], uint8_t dispatch[32]) {
    MicroWord h[CONTROL_STORE_SIZE];
    buildControlStore(mode, h, dispatch);          // same microprogram, same entry addresses
    for (size_t i = 0; i < CONTROL_STORE_SIZE; ++i) rom[i] = encodeMicroWord(h[i]);

    if (auto err = verifyVerticalStore(h, rom); !err.empty())
        throw std::logic_error("vertical control store does not match the horizontal one: " + err);
}

std::string verifyVerticalStore(const MicroWord hrom[], const VMicroWord vrom[]) {
    for (size_t i = 0; i < CONTROL_STORE_SIZE; ++i) {
        std::string err;
        MicroWord d = decodeMicroWord(vrom[i], &err);
        const MicroWord& h = hrom[i];
        auto at = [&](const char* f) { return std::string("uPC ") + std::to_string(i) + ": field " + f + " differs"; };
        if (!err.empty()) return "uPC " + std::to_string(i) + ": " + err;
        if (d.valid != h.valid) return at("valid");
        if (!h.valid) continue;
        if (d.stage != h.stage) return at("stage");
        if (d.seq != h.seq) return at("seq");
        if (d.readRs1 != h.readRs1) return at("readRs1");
        if (d.readRs2 != h.readRs2) return at("readRs2");
        if (d.readRd != h.readRd) return at("readRd");
        if (d.readRa != h.readRa) return at("readRa");
        if (d.unaryOp != h.unaryOp) return at("unaryOp");
        if (d.aluEnable != h.aluEnable) return at("aluEnable");
        if (d.updateFlags != h.updateFlags) return at("updateFlags");
        if (d.trapDivZero != h.trapDivZero) return at("trapDivZero");
        if (d.trapOverflow != h.trapOverflow) return at("trapOverflow");
        if (d.linkPC != h.linkPC) return at("linkPC");
        if (d.destIsRA != h.destIsRA) return at("destIsRA");
        if (d.stopFetch != h.stopFetch) return at("stopFetch");
        if (d.branch != h.branch) return at("branch");
        if (d.memRead != h.memRead) return at("memRead");
        if (d.memWrite != h.memWrite) return at("memWrite");
        if (d.regWrite != h.regWrite) return at("regWrite");
        if (d.halt != h.halt) return at("halt");
        // aluOp only carries meaning when the ALU is enabled in an EX word
        if (h.stage == MicroStage::EX && d.aluOp != h.aluOp) return at("aluOp");
    }
    return "";
}

std::string vfieldBits(VMicroWord v) {
    std::string s;
    for (int b = 15; b >= 0; --b) {
        s += ((v >> b) & 1) ? '1' : '0';
        if (b == 13 || b == 12) s += '|';
    }
    return s;
}

std::string describeVertical(VMicroWord v) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%04X", v);
    return std::string(hex) + "  " + vfieldBits(v) + "  " + describe(decodeMicroWord(v));
}
