// Compile/run via make test-branch. Deterministic reference/differential tests.
#include "cpu.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
using namespace risc201;
static unsigned checks=0, executions=0;
static void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
static uint32_t imm(OperationCode op, unsigned rd, unsigned rs, int v) {
    DecodedInstruction d; d.operation=op; d.destinationRegister=rd; d.sourceRegister1=rs;
    d.usesConstantNumber=true; d.rawConstantValue=static_cast<uint16_t>(v); return convertToBinary(d);
}
static uint32_t plain(OperationCode op) { DecodedInstruction d; d.operation=op; return convertToBinary(d); }
static uint32_t branch(OperationCode op, int bytes) {
    DecodedInstruction d; d.operation=op; d.branchJumpDistance=bytes; return convertToBinary(d);
}
struct State {
    std::array<int32_t,16> reg{}; std::vector<uint8_t> mem;
    bool eq=false,gt=false,halt=false,fault=false; uint64_t retired=0,cycles=0,stalls=0,flushes=0;
    BranchPredictor::Stats stats; std::string faultName;
};
static State run(const std::vector<uint32_t>& code, PipelineMode mode, ControlUnit cu,
                 bool enabled, int trainPC=-1) {
    auto cpu=std::make_unique<CPU>(); cpu->setMode(mode); cpu->setControlUnit(cu);
    cpu->recordTimeline=false; cpu->alu.timingEnabled=false; cpu->branchPredictor.enabled=enabled;
    std::copy(code.begin(),code.end(),cpu->instrMem.begin()); cpu->programWords=code.size();
    if (enabled && trainPC>=0) for (int i=0;i<3;++i)
        cpu->branchPredictor.resolve(trainPC,true,{0,false},4,false);
    State s; ++executions;
    try { while (!cpu->halted && !cpu->faulted && cpu->totalCycles<20000) cpu->tick(); }
    catch(const SimException& e) { s.faultName=::faultName(e.type()); }
    check(cpu->halted || cpu->faulted,"unexpected timeout");
    std::copy(std::begin(cpu->reg),std::end(cpu->reg),s.reg.begin()); s.mem=cpu->dataMem;
    s.eq=cpu->flags.IsEqual;s.gt=cpu->flags.IsGreaterThan;s.halt=cpu->halted;s.fault=cpu->faulted;
    s.retired=cpu->instructionsRetired;s.cycles=cpu->totalCycles;s.stalls=cpu->stallCycles;
    s.flushes=cpu->branchFlushes;s.stats=cpu->branchPredictor.stats;return s;
}
static bool same(const State& a,const State& b) {
    return a.reg==b.reg && a.mem==b.mem && a.eq==b.eq && a.gt==b.gt && a.halt==b.halt
        && a.fault==b.fault && a.faultName==b.faultName && a.retired==b.retired;
}
static void differential(const std::vector<uint32_t>& code, int expectedR2=-1, int trainPC=-1) {
    for (auto cu:{ControlUnit::HORIZONTAL,ControlUnit::VERTICAL}) {
        State baseline;
        for (auto mode:{PipelineMode::STAGE_4,PipelineMode::STAGE_6}) {
            const auto off=run(code,mode,cu,false),on=run(code,mode,cu,true,trainPC);
            check(same(off,on),"OFF/ON architectural mismatch");
            if (expectedR2>=0) check(on.reg[2]==expectedR2,"wrong expected register result");
            if (mode==PipelineMode::STAGE_4) baseline=off;
            else if (!off.fault) check(same(baseline,off),"pipeline architectural mismatch");
        }
    }
}
int main() {
    try {
        BranchPredictor bp; bp.enabled=true;
        std::array<uint8_t,256> reference; reference.fill(1);
        std::mt19937 random(0x2b17);
        // Exhaust counter transitions, saturation, packed-neighbor isolation and aliasing.
        for (unsigned k=0;k<100000;++k) {
            uint32_t pc=(random()%4096)*4; unsigned i=(pc>>2)&255; bool taken=random()&1;
            const auto pred=bp.predict(pc,branch(OperationCode::BEQ,-16));
            check(pred.taken==(reference[i]>=2),"counter prediction mismatch");
            check(pred.nextPC==(pred.taken ? pc-16 : pc+4),"target extraction mismatch");
            bp.resolve(pc,taken,pred,taken ? pc-16 : pc+4,pred.taken!=taken);
            reference[i]=taken ? std::min(3,int(reference[i])+1) : std::max(0,int(reference[i])-1);
            check(bp.counter(pc)==reference[i],"counter update mismatch");
            check(bp.counter((i^1u)*4)==reference[i^1u],"packed neighbor corrupted");
        }
        bp.reset(); check(bp.enabled && bp.stats.conditional==0,"reset changed configuration/stats");
        for(unsigned i=0;i<256;++i) check(bp.counter(i*4)==1,"cold state mismatch");
        bp.enabled=false;
        for(int k=0;k<10;++k) { auto pr=bp.predict(4,branch(OperationCode::BEQ,-4)); bp.resolve(4,true,pr,0,true); }
        check(bp.counter(4)==1 && bp.stats.correct==0,"OFF trained or predicted taken");
        const std::vector<uint32_t> loop={imm(OperationCode::MOV,1,0,100),imm(OperationCode::MOV,2,0,0),
            imm(OperationCode::ADD,2,2,1),imm(OperationCode::SUB,1,1,1),imm(OperationCode::CMP,0,1,0),
            branch(OperationCode::BGT,-12),plain(OperationCode::HALT)};
        differential(loop,100);
        for(auto cu:{ControlUnit::HORIZONTAL,ControlUnit::VERTICAL}) for(auto mode:{PipelineMode::STAGE_4,PipelineMode::STAGE_6}) {
            auto off=run(loop,mode,cu,false),on=run(loop,mode,cu,true);
            check(off.stats.conditional==100 && on.stats.conditional==100,"wrong branch accounting");
            check(on.stats.correct==98 && on.stats.conditionalFlushes==2,"loop training/recovery incorrect");
            check(on.cycles<off.cycles,"predictable loop did not improve cycles");
        }
        // Correctly predicted path may contain another branch immediately.
        differential({imm(OperationCode::CMP,0,0,0),branch(OperationCode::BEQ,8),0xffffffff,
            branch(OperationCode::BEQ,8),0xffffffff,plain(OperationCode::HALT)},0);
        // Load-use stall before a compare/conditional branch.
        differential({imm(OperationCode::MOV,1,0,7),imm(OperationCode::ST,1,0,0),imm(OperationCode::LD,3,0,0),
            imm(OperationCode::CMP,0,3,7),branch(OperationCode::BEQ,8),0xffffffff,
            imm(OperationCode::MOV,2,0,42),plain(OperationCode::HALT)},42);
        // Trained predicted-T/actual-N: invalid speculative target is flushed, not raised.
        differential({imm(OperationCode::CMP,0,0,1),branch(OperationCode::BEQ,-8),
            imm(OperationCode::MOV,2,0,9),plain(OperationCode::HALT)},9,4);
        // Same with a misaligned target, and direction disagreement but identical next PC.
        differential({imm(OperationCode::CMP,0,0,1),branch(OperationCode::BEQ,1),plain(OperationCode::HALT)},0,4);
        differential({imm(OperationCode::CMP,0,0,0),branch(OperationCode::BEQ,4),plain(OperationCode::HALT)},0);
        // Wrong-path HALT and illegal opcode must never retire/fault.
        differential({imm(OperationCode::CMP,0,0,0),branch(OperationCode::BEQ,12),plain(OperationCode::HALT),
            0xffffffff,imm(OperationCode::MOV,2,0,31),plain(OperationCode::HALT)},31);
        // Real invalid branch target still faults; same-pipeline architectural parity.
        differential({imm(OperationCode::CMP,0,0,0),branch(OperationCode::BEQ,1),plain(OperationCode::HALT)});
        differential({imm(OperationCode::CMP,0,0,0),branch(OperationCode::BEQ,400),plain(OperationCode::HALT)});
        differential({imm(OperationCode::DIV,2,0,0),plain(OperationCode::HALT)});
        // Repeated loop with alternating inner outcomes; skips memory writes on wrong paths.
        differential({imm(OperationCode::MOV,1,0,40),imm(OperationCode::MOV,2,0,0),
            imm(OperationCode::AND,3,1,1),imm(OperationCode::CMP,0,3,0),branch(OperationCode::BEQ,8),
            imm(OperationCode::ADD,2,2,1),imm(OperationCode::SUB,1,1,1),imm(OperationCode::CMP,0,1,0),
            branch(OperationCode::BGT,-24),imm(OperationCode::ST,2,0,0),plain(OperationCode::HALT)},20);
        // Two distinct static branches deliberately alias in the 256-entry table.
        std::vector<uint32_t> alias(260,plain(OperationCode::NOP));
        alias[0]=imm(OperationCode::MOV,1,0,5);alias[1]=imm(OperationCode::SUB,1,1,1);
        alias[2]=imm(OperationCode::CMP,0,1,0);alias[3]=branch(OperationCode::BGT,1024);
        alias[4]=plain(OperationCode::HALT);alias[259]=branch(OperationCode::BGT,-1032);
        differential(alias,0);
        // Deterministic random forward branches inside a finite outer loop.
        for(unsigned seed=0;seed<120;++seed) {
            std::vector<uint32_t> code{imm(OperationCode::MOV,1,0,5),imm(OperationCode::MOV,2,0,0)};
            for(unsigned j=0;j<20;++j) {
                code.push_back(imm(OperationCode::CMP,0,1,random()%7));
                code.push_back(branch(random()&1 ? OperationCode::BEQ : OperationCode::BGT,8));
                code.push_back(imm(OperationCode::ADD,2,2,1));
            }
            code.push_back(imm(OperationCode::SUB,1,1,1));code.push_back(imm(OperationCode::CMP,0,1,0));
            code.push_back(branch(OperationCode::BGT,-static_cast<int>((code.size()-2)*4)));
            code.push_back(imm(OperationCode::ST,2,0,0));code.push_back(plain(OperationCode::HALT));
            differential(code);
        }
        std::cout<<"PASS: "<<checks<<" checks, "<<executions<<" CPU executions (4/6-stage, horizontal/vertical, OFF/ON).\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1; }
}
