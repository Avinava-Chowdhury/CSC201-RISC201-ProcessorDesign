# Differential fuzz test: random programs run on both pipelines are compared with a reference model.
# Run from the project root after "make":   python3 tests/fuzz_pipelines.py 150
import random, subprocess, re, sys
M=0xFFFFFFFF
def s32(x): x&=M; return x-(1<<32) if x&0x80000000 else x
def gen(seed):
    r=random.Random(seed); L=["sub sp, sp, 64"]; R=[0]*16; mem={}; sp=-64
    # python model
    R=[0]*16; R[14]=0x10000-64; 
    def val(): return r.randint(0,100)
    for k in range(1,8): L.append(f"mov r{k}, {k*3}"); R[k]=k*3
    for _ in range(40):
        op=r.choice(["add","sub","and","or","xor","mul","lsl","lsr","mov","st","ld","addi"])
        d,a,b=r.randint(1,7),r.randint(1,7),r.randint(1,7); imm=r.randint(0,9)
        if op in("add","sub","and","or","xor","mul"):
            L.append(f"{op} r{d}, r{a}, r{b}")
            x,y=R[a],R[b]
            R[d]={"add":x+y,"sub":x-y,"and":x&y,"or":x|y,"xor":x^y,"mul":x*y}[op]; R[d]=s32(R[d])
        elif op=="addi": L.append(f"add r{d}, r{a}, {imm}"); R[d]=s32(R[a]+imm)
        elif op=="lsl": L.append(f"lsl r{d}, r{a}, {imm}"); R[d]=s32((R[a]&M)<<imm)
        elif op=="lsr": L.append(f"lsr r{d}, r{a}, {imm}"); R[d]=s32((R[a]&M)>>imm)
        elif op=="mov": L.append(f"mov r{d}, {imm}"); R[d]=imm
        elif op=="st": o=4*r.randint(0,15); L.append(f"st r{a}, {o}[sp]"); mem[o]=R[a]
        elif op=="ld":
            o=4*r.randint(0,15)
            if o in mem: L.append(f"ld r{d}, {o}[sp]"); R[d]=mem[o]
    L.append("halt")
    return "\n".join(L)+"\n",R
bad={4:0,6:0}; n=int(sys.argv[1]); first={}
for seed in range(n):
    src,R=gen(seed); open("/tmp/f.s","w").write(src)
    subprocess.run(["./asm_tool","/tmp/f.s","/tmp/f.hex","--hex"],capture_output=True)
    for m in (4,6):
        out=subprocess.run(["./sim"],input=f"mode {m}\nload /tmp/f.hex\ncontinue\nregs\nquit\n",capture_output=True,text=True).stdout
        regs={int(a):int(b,16) for a,b in re.findall(r"r(\d+)\s*:\s*(0x[0-9a-f]+)",out)}
        exp={k:R[k]&M for k in range(1,8)}
        got={k:regs.get(k) for k in range(1,8)}
        if exp!=got or "FAULT" in out:
            bad[m]+=1; first.setdefault(m,(seed,[(k,hex(exp[k]),hex(got[k]) if got[k] is not None else None) for k in exp if exp[k]!=got[k]][:3],"FAULT" in out))
print("programs:",n,"mismatches:",bad); print("first mismatch per mode:",first)
