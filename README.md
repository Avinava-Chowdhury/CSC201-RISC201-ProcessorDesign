# CSC201-RISC201-ProcessorDesign
# RISC201 Instruction Set Architecture (`isa.h`) Detailed Code Explanation

This part of the document explains the internal C++ structures, enumerations, and constants defined in `isa.h` used to model the RISC201 hardware architecture.

## 1. Processor Architecture Constants
The `constexpr` values define the immutable hardware boundaries of the processor.
* **Registers:** The processor contains 16 total registers (`TOTAL_REGISTERS`), all 32 bits wide (`BYTES_PER_WORD`). Register 14 is dedicated as the Stack Pointer (`STACK_POINTER_REG_INDEX`), and Register 15 holds the Return Address (`RETURN_ADDRESS_REG_INDEX`).
* **Memory Constraints:** Memory is divided into two segments: 16 KB for instructions (`INSTRUCTION_MEMORY_SIZE`) and 64 KB for data and the stack (`DATA_MEMORY_SIZE`).
* **Stack Boundaries:** The stack grows downward from `STACK_STARTING_ADDRESS` (0x10000) and is bounded by `STACK_LOWEST_LIMIT` (0x0C000), preventing it from exceeding 16 KB of usage.
* **Helper Functions:** `getRegisterName` and `parseRegisterName` convert between human-readable register strings (e.g., "r7", "sp") and their integer indices.

## 2. Operation Codes (`OperationCode`)
This `enum class` maps every supported assembly instruction to a unique 5-bit hardware opcode. It includes standard arithmetic (`ADD`, `SUB`, `MUL`, `DIV`, `MOD`), logic operations (`AND`, `OR`, `XOR`, `NOT`, `LSL`, `LSR`, `ASR`), memory access (`LD`, `ST`), control flow (`BEQ`, `BGT`, `B`, `CALL`, `RET`), and utility operations (`CMP`, `NOP`, `MOV`). 

## 3. Instruction Layouts and Syntax
Instructions are strictly 32 bits long, partitioned into structural categories to guide the hardware decoder.
* **`InstructionLayout`:** Defines the two core 32-bit structures:
  * `OPERATE`: Used for math, logic, and memory. Contains fields for an opcode, an immediate flag, a destination register, and source registers or constants.
  * `BRANCH`: Used for control flow. Contains only an opcode and a massive 27-bit offset distance.
* **`AssemblySyntax`:** Categorizes how the raw fields map to human-readable assembly. For example, `THREE_ARG_MATH` maps to instructions requiring a destination and two sources, while `STORE_MEMORY` indicates the destination field actually holds the data to be written.
* **`ImmediateModifier`:** Dictates how 16-bit constants embedded in instructions are expanded into full 32-bit values for the ALU. They can be sign-extended (`SIGNED`), zero-extended (`UNSIGNED`), or shifted to the upper 16 bits (`UPPER`).
* **`OperationDetails`:** A lookup table structure linking an instruction's string name (e.g., "add") to its hardware `OperationCode`, `InstructionLayout`, `AssemblySyntax`, and valid modifiers.

## 4. The Decoded Instruction (`DecodedInstruction`)
This structure represents a single 32-bit instruction broken down into isolated variables for pipeline routing.
* **Fields:** It stores the primary `operation`, up to two source registers (`sourceRegister1`, `sourceRegister2`), and one `destinationRegister`. 
* **Constants:** If the instruction uses an embedded number instead of a second source register, `usesConstantNumber` is true, and the value is stored in `rawConstantValue` with its `constantModifier`.
* **Branching:** If it is a jump instruction, the distance is stored in `branchJumpDistance`.
* **Execution/Expansion Functions:** `calculateFinalConstantValue()` computes the final 32-bit number based on the modifier. Bit-level functions like `extendSignBit` convert smaller binary chunks into full 32-bit signed integers.
* **Binary Translation:** The `convertToBinary` and `convertFromBinary` functions pack and unpack this struct into the final 32-bit unsigned integer (`uint32_t`) executed by the CPU.

## 5. Processor Status Flags (`ProcessorFlags`)
A struct representing the hidden architectural flags set exclusively by the `CMP` instruction.
* `IsEqual`: Set to true if the compared inputs are identical. Read by `BEQ`.
* `IsGreaterThan`: Set to true if the first input strictly exceeds the second using signed 32-bit comparison. Read by `BGT`.
* `evaluateComparison`: A function that takes two 32-bit integers and computes the resulting flags.

## 6. Pipeline Hazard Classification Helpers
A suite of boolean functions (e.g., `isMemoryLoad`, `isConditionalBranch`, `modifiesDestinationRegister`) that accept an `OperationCode` or `DecodedInstruction`. These are strictly used by the CPU control unit to detect pipeline hazards (like trying to read a register before a previous instruction has finished writing to it) and coordinate clock cycles across the 4-stage and 6-stage architectures.

# Stack safety and runtime faults

The stack is full descending: it starts at `0x10000` (empty) and grows down towards `0xC000`, which gives 16 KB. `sp` is r14. `push` and `pop` are assembler macros that expand into `sub`/`st` and `ld`/`add` on `sp`.

The simulator checks the stack while the program runs. When a check fails it throws a `SimException` with a fault type, the PC of the instruction that caused it, and the bad address. The CPU then halts until `reset`.

| Fault | When it happens |
|---|---|
| `STACK_OVERFLOW` | `sp` is about to go below `0xC000` |
| `STACK_UNDERFLOW` | `sp` is about to go above `0x10000` (more pops than pushes) |
| `MISALIGNED` | `sp`, or any word load/store address, is not a multiple of 4 |
| `STACK_CORRUPTION` | a store lands in the stack segment but below `sp` (the free part of the stack) |
| `OUT_OF_BOUNDS` | a load/store goes outside the 64 KB data memory |
| `INVALID_OPCODE` | the decoder meets an opcode that doesn't exist |

The new `sp` value is checked before it is written back, so a faulting instruction never leaves `sp` pointing at an illegal address.

Example from the CLI:

```
(sim) > step 40
*** HARDWARE FAULT: STACK_OVERFLOW ***
  reason  : stack grew past its 16 KB limit (floor is 0x0000c000)
  at PC   : 0x0000000c
  address : 0x0000bffc
CPU halted. Type 'reset' to start over.
```

Run the stack tests with `make test`. The programs are in `tests/stack/`.
