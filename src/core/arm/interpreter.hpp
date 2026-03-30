#pragma once

#include "../../common/common_types.hpp"
#include <cstdio>
#include <functional>

class MemorySystem;

/// SVC dispatch function: receives SVC number (imm24), returns whether to continue execution
using SvcHandler = std::function<bool(u32)>;

/// CPSR condition flags (bits 31-28)
namespace CpsrFlags {
constexpr u32 N = 1u << 31;
constexpr u32 Z = 1u << 30;
constexpr u32 C = 1u << 29;
constexpr u32 V = 1u << 28;
}

/// ARM11 CPU state (simplified, single mode)
struct ArmState {
    u32 r[16];   // R0-R15; r[15] is PC
    u32 cpsr{0}; // Current Program Status Register (N,Z,C,V in bits 31-28)
    u32 fpscr{0}; // Floating-Point Status and Control Register (only NZCV is currently used)
    u32 s[32]{};  // VFP single-precision registers S0..S31 (raw IEEE-754 bits)
};

/// ARM11 interpreter - fetch-decode-execute loop.
/// Supports minimal instruction set; unknown instructions are logged and skipped.
class ArmInterpreter {
public:
    ArmInterpreter(MemorySystem& memory, SvcHandler svc_handler);
    ~ArmInterpreter() = default;

    ArmState& state() { return state_; }
    const ArmState& state() const { return state_; }

    /// Run up to max_instructions. Returns number of instructions executed.
    u32 runSlice(u32 max_instructions);

    /// Run until cycles_limit cycles consumed. Returns cycles used (1 per instruction).
    u64 runSliceWithCycles(u64 cycles_limit);

    /// Set PC, SP, and LR (typically called at bootstrap).
    void setPC(VAddr pc) { state_.r[15] = pc; }
    void setSP(VAddr sp) { state_.r[13] = sp; }
    void setLR(VAddr lr) { state_.r[14] = lr; }
    VAddr getPC() const { return state_.r[15]; }

    void writeReg(u32 reg, u32 value);

    u32 fetch32();
    bool execute();
    void advancePC() { state_.r[15] += 4; }

    MemorySystem& memory_;
    SvcHandler svc_handler_;
    ArmState state_;
    bool exclusive_valid_{false};
    u32 exclusive_addr_{0};
};
