#include "interpreter.hpp"
#include "../3ds/memory.hpp"
#include "../../common/logger.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// Off by default; set to true only when debugging (e.g. after SVC to trace next instructions).
static bool s_log_reg_writes = false;
static bool s_trace_enabled = false;

void logRegWrite(u32 reg, u32 value, u32 pc) {
    if (s_log_reg_writes && reg <= 14)
        Logger::log("  REG W R%u = 0x%08X  (PC=0x%08X)\n", reg, value, pc);
}

constexpr u32 STACK_TRACE_LO = 0x0F000000u;
constexpr u32 STACK_TRACE_HI = 0x10000000u;

void logStackRegionWrite(u32 addr, u32 value, u32 sp, u32 pc) {
    if (addr >= STACK_TRACE_LO && addr < STACK_TRACE_HI)
        Logger::log("Stack write addr=0x%08X value=0x%08X SP(R13)=0x%08X PC=0x%08X\n", addr, value, sp, pc);
}

}  // namespace

namespace {

// --- Instruction bit positions (ARM ARM) ---
enum InstBit : unsigned {
    BITS_COND = 28,
    BITS_OPCODE1 = 21,
    BITS_OPCODE2 = 4,
    BIT_I = 25,
    BIT_S = 20,
    BIT_P = 24,
    BIT_U = 23,
    BIT_B = 22,
    BIT_W = 21,
    BIT_L = 20,
};

constexpr u32 cond(uint32_t inst) { return inst >> BITS_COND; }
constexpr u32 opcode1(uint32_t inst) { return (inst >> BITS_OPCODE1) & 0xF; }
constexpr u32 Rd(uint32_t inst) { return (inst >> 12) & 0xF; }
constexpr u32 Rn(uint32_t inst) { return (inst >> 16) & 0xF; }
constexpr u32 Rm(uint32_t inst) { return inst & 0xF; }
constexpr u32 imm24(uint32_t inst) { return inst & 0xFFFFFF; }
constexpr bool I(uint32_t inst) { return (inst & (1u << BIT_I)) != 0; }
constexpr bool S(uint32_t inst) { return (inst & (1u << BIT_S)) != 0; }
constexpr bool P(uint32_t inst) { return (inst & (1u << BIT_P)) != 0; }
constexpr bool U(uint32_t inst) { return (inst & (1u << BIT_U)) != 0; }
constexpr bool B(uint32_t inst) { return (inst & (1u << BIT_B)) != 0; }
constexpr bool W(uint32_t inst) { return (inst & (1u << BIT_W)) != 0; }
constexpr bool L(uint32_t inst) { return (inst & (1u << BIT_L)) != 0; }

// Data-processing immediate: 12-bit value is [imm4:rot:imm8] and gets expanded (barrel shifter).
constexpr u32 imm12(uint32_t inst) { return inst & 0xFFF; }
// LDR/STR single transfer: offset is strictly the 12-bit unsigned immediate (bits [11:0]). No expansion.
constexpr u32 ldrStrOffset12(uint32_t inst) { return inst & 0xFFF; }

constexpr u32 COND_EQ = 0, COND_NE = 1, COND_CS = 2, COND_CC = 3, COND_MI = 4, COND_PL = 5;
constexpr u32 COND_VS = 6, COND_VC = 7, COND_HI = 8, COND_LS = 9, COND_GE = 10, COND_LT = 11;
constexpr u32 COND_GT = 12, COND_LE = 13, COND_AL = 14, COND_NV = 15;

// Data-processing opcodes (opcode1) - used for flag logic and readability
constexpr u32 OP_SUB = 0x2, OP_ADD = 0x4, OP_ADC = 0x5, OP_SBC = 0x6, OP_RSC = 0x7;
constexpr u32 OP_CMP = 0xA, OP_CMN = 0xB;

// Decoder masks (only the bits that identify the instruction class)
constexpr u32 MASK_COND_OP1_S_Rd = 0x0FC000F0u;  // cond(4) opcode1(4) S(1) 0000 Rd(4)
constexpr u32 MASK_DATAPROC = 0x0C000000u;
constexpr u32 MASK_DATAPROC_IMM = 0x0E000000u;
constexpr u32 MASK_LDRSTR = 0x0DE00000u;
constexpr u32 MASK_MOV_OP = 0x00E00000u;  // opcode bits 24-21 only (op1)
constexpr u32 VAL_MOV_OP = 0x00D00000u;   // op1=0xD = MOV (register)
constexpr u32 MASK_BLX_REG = 0x0FFFFFF0u;
constexpr u32 MASK_NOP = 0x0FFFFFF0u;
constexpr u32 MASK_SVC = 0x0F000000u;   // SVC: bits 27-24 = 1111
constexpr u32 VAL_SVC = 0x0F000000u;

inline int32_t signExtend24(u32 v) {
    return static_cast<int32_t>((v & 0x800000) ? (v | 0xFF000000) : v);
}

// Trace: log next N instructions when PC hits either trigger (loop at 0x00100004 / 0x001045B4).
constexpr u32 TRACE_TRIGGER_PC = 0x0010003Cu;
constexpr u32 TRACE_TRIGGER_PC_LOOP = 0x001045B4u;
constexpr int TRACE_INSTRUCTION_COUNT = 20;
static int s_trace_remaining = -1;

void logArmDisasm(u32 pc, u32 inst, int index, bool is_trigger_pc,
                  const u32* r, u32 cpsr) {
    char mnem[80];
    const u32 bits27_25 = (inst >> 25) & 7;
    const u32 c = cond(inst);
    const u32 n = (cpsr >> 31) & 1u, z = (cpsr >> 30) & 1u, c_flag = (cpsr >> 29) & 1u, v = (cpsr >> 28) & 1u;

    if ((inst & 0xF0000000u) == 0xF0000000u && (inst & 0x0F000000u) == 0x0F000000u) {
        std::snprintf(mnem, sizeof(mnem), "SVC #0x%06X", inst & 0xFFFFFFu);
    } else if (bits27_25 == 5) {
        const int32_t off = signExtend24(inst & 0xFFFFFFu) * 4;
        const u32 target = pc + 8 + off;
        std::snprintf(mnem, sizeof(mnem), "%s 0x%08X (offset %d)",
                      (inst & (1u << 24)) ? "BL" : "B", target, off);
    } else if (bits27_25 == 2) {
        const u32 rn = (inst >> 16) & 0xFu;
        const u32 rd = (inst >> 12) & 0xFu;
        const u32 off12 = inst & 0xFFFu;
        const char* load = (inst & (1u << 20)) ? "LDR" : "STR";
        const char* size = (inst & (1u << 22)) ? "B" : "";
        std::snprintf(mnem, sizeof(mnem), "%s%s R%u, [R%u, #%u]",
                      load, size, rd, rn, off12);
    } else if ((inst & 0x0FFFFFF0u) == 0x012FFF10u) {
        const u32 rm = inst & 0xFu;
        std::snprintf(mnem, sizeof(mnem), "%s R%u", (inst & (1u << 5)) ? "BLX" : "BX", rm);
    } else if (bits27_25 == 4) {
        const u32 rn = (inst >> 16) & 0xFu;
        const u32 list = inst & 0xFFFFu;
        const char* ld = (inst & (1u << 20)) ? "LDM" : "STM";
        const char* mode = (inst & (1u << 23)) ? ((inst & (1u << 24)) ? "IB" : "IA") : ((inst & (1u << 24)) ? "DB" : "DA");
        std::snprintf(mnem, sizeof(mnem), "%s%s R%u%s, {0x%04X}", ld, mode, rn, (inst & (1u << 21)) ? "!" : "", list);
    } else if (bits27_25 == 0 || bits27_25 == 1) {
        const u32 op1 = (inst >> 21) & 0xFu;
        const u32 rd = (inst >> 12) & 0xFu;
        const u32 rn = (inst >> 16) & 0xFu;
        static const char* opnames[] = {"AND","EOR","SUB","RSB","ADD","ADC","SBC","RSC",
                                       "TST","TEQ","CMP","CMN","ORR","MOV","BIC","MVN"};
        const char* op = (op1 < 16u) ? opnames[op1] : "???";
        std::snprintf(mnem, sizeof(mnem), "%s R%u,R%u,... (op1=0x%X)", op, rd, rn, op1);
    } else {
        std::snprintf(mnem, sizeof(mnem), "??? (bits27_25=%u cond=%u)", bits27_25, c);
    }

    const char* note = is_trigger_pc ? " (state=before)" : "";
    Logger::log("  [%2d] PC=0x%08X inst=0x%08X %s | R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X R8=0x%08X R9=0x%08X R10=0x%08X R11=0x%08X R12=0x%08X SP=0x%08X LR=0x%08X PC=0x%08X | NZCV=%u%u%u%u%s\n",
                index, pc, inst, mnem, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15], n, z, c_flag, v, note);
}

bool conditionPassed(u32 cond_bits, u32 cpsr) {
    const bool n = (cpsr & CpsrFlags::N) != 0;
    const bool z = (cpsr & CpsrFlags::Z) != 0;
    const bool c = (cpsr & CpsrFlags::C) != 0;
    const bool v = (cpsr & CpsrFlags::V) != 0;

    switch (cond_bits) {
        case COND_EQ: return z;
        case COND_NE: return !z;
        case COND_CS: return c;
        case COND_CC: return !c;
        case COND_MI: return n;
        case COND_PL: return !n;
        case COND_VS: return v;
        case COND_VC: return !v;
        case COND_HI: return c && !z;
        case COND_LS: return !c || z;
        case COND_GE: return n == v;
        case COND_LT: return n != v;
        case COND_GT: return !z && (n == v);
        case COND_LE: return z || (n != v);
        case COND_AL: return true;
        case COND_NV: return false;
        default: return false;
    }
}

void setCpsrFlags(u32& cpsr, bool n, bool z, bool c, bool v) {
    constexpr u32 NZCV_MASK = CpsrFlags::N | CpsrFlags::Z | CpsrFlags::C | CpsrFlags::V;
    cpsr = (cpsr & ~NZCV_MASK)
         | (n ? CpsrFlags::N : 0u)
         | (z ? CpsrFlags::Z : 0u)
         | (c ? CpsrFlags::C : 0u)
         | (v ? CpsrFlags::V : 0u);
}

u32 expandImm12(u32 imm12_val) {
    u32 rot = (imm12_val >> 8) & 0xF;
    u32 imm8 = imm12_val & 0xFF;
    if (rot == 0) return imm8;
    u32 val = imm8 | (imm8 << 16);
    return (val >> (rot * 2)) | (val << (32 - rot * 2));
}

// Second operand for data-processing only. Do not use for LDR/STR offset.
u32 getOp2DataProcessing(ArmInterpreter& cpu, u32 inst) {
    if (I(inst)) {
        return expandImm12(imm12(inst));
    }
    u32 rm_val = cpu.state().r[Rm(inst)];
    constexpr u32 SHIFT_TYPE_MASK = 3u;
    constexpr u32 SHIFT_IMM_MASK = 0x1Fu;
    u32 shift_type = (inst >> 5) & SHIFT_TYPE_MASK;
    u32 shift_imm = (inst >> 7) & SHIFT_IMM_MASK;
    switch (shift_type) {
        case 0: return shift_imm ? (rm_val << shift_imm) : rm_val;
        case 1: return shift_imm ? (rm_val >> shift_imm) : rm_val;
        case 2: return static_cast<u32>(static_cast<s32>(rm_val) >> (shift_imm ? shift_imm : 32));
        case 3: return (rm_val >> shift_imm) | (rm_val << (32 - shift_imm));
        default: return rm_val;
    }
}

inline bool isSubtractionOpcode(u32 op1) {
    return op1 == OP_SUB || op1 == OP_SBC || op1 == OP_RSC || op1 == OP_CMP;
}

inline bool isAdditionOpcode(u32 op1) {
    return op1 == OP_ADD || op1 == OP_ADC || op1 == OP_CMN;
}

// Carry for subtraction: C = 1 if no borrow, i.e. (src1 >= src2) unsigned.
inline bool carryFromSub(u32 src1, u32 src2) {
    return src1 >= src2;
}

// Carry for addition: C = 1 if carry out from bit 31.
inline bool carryFromAdd(u32 src1, u32 src2, u32 result) {
    return result < src1;
}

// Overflow for subtraction: V = (sign Rn != sign Op2) and (sign result != sign Rn).
inline bool overflowFromSub(u32 src1, u32 src2, u32 result) {
    return ((src1 ^ src2) & (src1 ^ result) & 0x80000000u) != 0;
}

// Overflow for addition: V = (sign Rn == sign Op2) and (sign result != sign Rn).
inline bool overflowFromAdd(u32 src1, u32 src2, u32 result) {
    return ((src1 ^ result) & (src2 ^ result) & 0x80000000u) != 0;
}

}  // namespace

ArmInterpreter::ArmInterpreter(MemorySystem& memory, SvcHandler svc_handler)
    : memory_(memory), svc_handler_(std::move(svc_handler)), state_{} {
    for (u32 i = 0; i < 16; ++i) {
        state_.r[i] = 0;
    }
}

void ArmInterpreter::writeReg(u32 reg, u32 value) {
    logRegWrite(reg, value, state_.r[15]);
    state_.r[reg] = value;
}

u32 ArmInterpreter::fetch32() {
    return memory_.read32(state_.r[15]);
}

u32 ArmInterpreter::runSlice(u32 max_instructions) {
    u32 executed = 0;
    while (executed < max_instructions) {
        if (!execute()) {
            break;
        }
        ++executed;
    }
    return executed;
}

u64 ArmInterpreter::runSliceWithCycles(u64 cycles_limit) {
    u64 cycles = 0;
    while (cycles < cycles_limit) {
        if (!execute()) {
            break;
        }
        ++cycles;
    }
    return cycles;
}

bool ArmInterpreter::execute() {
    const u32 pc = state_.r[15];
    if (pc >= 0x0FFFF000u && pc < 0x10000000u) {
        Logger::log("Stack Execution Detected - Probable Crash  PC=0x%08X  LR(R14)=0x%08X  SP(R13)=0x%08X\n",
                    pc, state_.r[14], state_.r[13]);
        return false;
    }
    const u32 inst = fetch32();
    const u32 cond_bits = cond(inst);

    if (s_trace_enabled && (pc == TRACE_TRIGGER_PC || pc == TRACE_TRIGGER_PC_LOOP) && s_trace_remaining < 0) {
        s_trace_remaining = TRACE_INSTRUCTION_COUNT;
        Logger::log("=== Trace: next %d instructions. State shown is BEFORE each instruction runs. ===\n", TRACE_INSTRUCTION_COUNT);
    }
    if (s_trace_enabled && s_trace_remaining >= 0) {
        const int index = TRACE_INSTRUCTION_COUNT - s_trace_remaining;
        logArmDisasm(pc, inst, index, pc == TRACE_TRIGGER_PC, state_.r, state_.cpsr);
        --s_trace_remaining;
    }

    // SVC: PC advanced before handler. Save and restore PC/LR so handler cannot corrupt return path.
    if ((inst & MASK_SVC) == VAL_SVC) {
        advancePC();
        if (cond_bits == COND_NV) return true;
        u32 svc_num = imm24(inst);
        const u32 saved_pc = state_.r[15];
        const u32 saved_lr = state_.r[14];
        if (svc_handler_) {
            const bool cont = svc_handler_(svc_num);
            state_.r[15] = saved_pc;
            state_.r[14] = saved_lr;
            return cont;
        }
        return true;
    }

    const u32 bits27_25 = (inst >> 25) & 7;

    // LDM/STM (bits 27-25 = 100). In ARMv7, cond=15 (0xF) is often "unconditional" for this class; do not skip.
    const bool is_ldm_stm = (bits27_25 == 4);
    if (!is_ldm_stm && !conditionPassed(cond_bits, state_.cpsr)) {
        if (s_trace_enabled && bits27_25 == 5) {
            const u32 n = (state_.cpsr >> 31) & 1u, z = (state_.cpsr >> 30) & 1u, c = (state_.cpsr >> 29) & 1u, v = (state_.cpsr >> 28) & 1u;
            Logger::log("  B cond=0x%X NOT TAKEN -> next inst  NZCV=%u%u%u%u\n", cond_bits, n, z, c, v);
        }
        advancePC();
        return true;
    }
    if (is_ldm_stm && cond_bits != COND_AL && cond_bits != COND_NV && !conditionPassed(cond_bits, state_.cpsr)) {
        advancePC();
        return true;
    }

    // B / BL: bits 27-25 = 101. Only R14 (LR) and R15 (PC) are written; R0-R13 are never touched.
    if (bits27_25 == 5) {
        const u32 cond_b = cond(inst);
        const bool taken = conditionPassed(cond_b, state_.cpsr);
        const u32 n = (state_.cpsr >> 31) & 1u, z = (state_.cpsr >> 30) & 1u, c = (state_.cpsr >> 29) & 1u, v = (state_.cpsr >> 28) & 1u;
        const int32_t offset_bytes = signExtend24(imm24(inst)) * 4;
        const u32 target = state_.r[15] + 8 + offset_bytes;
        if (s_trace_enabled && cond_b != COND_AL && cond_b != COND_NV)
            Logger::log("  B cond=0x%X %s -> 0x%08X  NZCV=%u%u%u%u\n", cond_b, taken ? "TAKEN" : "not taken", target, n, z, c, v);
        const bool is_bl = (inst & (1u << 24)) != 0;
        if (is_bl)
            writeReg(14, state_.r[15] + 4);
        state_.r[15] = target;
        return true;
    }

    if (bits27_25 == 2) {
        u32 rn = Rn(inst);
        u32 rd = Rd(inst);
        u32 base = (rn == 15) ? (state_.r[15] + 8) : state_.r[rn];
        // LDR/STR offset is strictly the 12-bit unsigned immediate [11:0]; no register/shift.
        u32 offset12 = ldrStrOffset12(inst);
        u32 offset_val = U(inst) ? offset12 : (0u - offset12);
        u32 addr = P(inst) ? (base + offset_val) : base;
        if (L(inst)) {
            if (B(inst)) {
                u32 val = memory_.read8(addr);
                writeReg(rd, val);
                if (s_trace_enabled && rn == 15)
                    Logger::log("  LDRB [PC,#%s%u] addr=0x%08X -> 0x%02X\n", U(inst) ? "" : "-", offset12, addr, val & 0xFFu);
            } else {
                u32 val = memory_.read32(addr);
                writeReg(rd, val);
                if (s_trace_enabled && rn == 15)
                    Logger::log("  LDR [PC,#%s%u] addr=0x%08X -> 0x%08X\n", U(inst) ? "" : "-", offset12, addr, val);
            }
        } else {
            if (B(inst)) {
                logStackRegionWrite(addr, state_.r[rd] & 0xFFu, state_.r[13], state_.r[15]);
                memory_.write8(addr, static_cast<u8>(state_.r[rd]));
            } else {
                logStackRegionWrite(addr, state_.r[rd], state_.r[13], state_.r[15]);
                memory_.write32(addr, state_.r[rd]);
            }
        }
        bool do_writeback = (!P(inst) || W(inst)) && (rn != 15);
        if (do_writeback)
            writeReg(rn, P(inst) ? addr : (base + offset_val));
        advancePC();
        return true;
    }

    // LDM/STM (bits 27-25 = 100). STMDB: base decremented before each store. LDMIA: load then increment.
    // Writeback: Rn becomes base +/- 4*count; if Rn is last in list for LDM, Rn keeps loaded value.
    if (bits27_25 == 4) {
        const u32 rn = (inst >> 16) & 0xFu;
        const u32 list = inst & 0xFFFFu;
        if (list == 0) {
            advancePC();
            return true;
        }
        const u32 base = (rn == 15) ? (state_.r[15] + 8) : state_.r[rn];
        int count = 0;
        for (u32 i = 0; i < 16; ++i)
            if (list & (1u << i)) ++count;
        const bool load = (inst & (1u << 20)) != 0;
        const bool up = (inst & (1u << 23)) != 0;   // U=1: increment (LDMIA/STMIA), U=0: decrement (LDMDB/STMDB)
        const bool pre = (inst & (1u << 24)) != 0;
        const bool wb = (inst & (1u << 21)) != 0;

        u32 addr;
        if (up) {
            addr = pre ? (base + 4) : base;                                    // IA: first at base
        } else {
            addr = base - 4u * static_cast<u32>(count);                        // DB: first at base-4*count
        }
        const u32 writeback_val = up ? (base + 4u * static_cast<u32>(count)) : (base - 4u * static_cast<u32>(count));

        if (load) {
            for (u32 i = 0; i < 16; ++i) {
                if (!(list & (1u << i))) continue;
                const u32 value = memory_.read32(addr);
                if (i == 15) {
                    Logger::log("LDM POP PC: loading value 0x%08X from addr 0x%08X into PC\n", value, addr);
                }
                writeReg(i, value);
                addr += 4;
            }
        } else {
            u32 store_vals[16];
            int idx = 0;
            for (u32 i = 0; i < 16; ++i) {
                if (!(list & (1u << i))) continue;
                store_vals[idx++] = state_.r[i];
            }
            u32 store_addr = addr;
            idx = 0;
            for (u32 i = 0; i < 16; ++i) {
                if (!(list & (1u << i))) continue;
                logStackRegionWrite(store_addr, store_vals[idx], state_.r[13], state_.r[15]);
                memory_.write32(store_addr, store_vals[idx++]);
                store_addr += 4;
            }
        }

        if (wb && rn != 15) {
            u32 last_in_list = 0;
            for (u32 i = 16; i-- > 0;)
                if (list & (1u << i)) { last_in_list = i; break; }
            if (!(list & (1u << rn)) || rn != last_in_list)
                writeReg(rn, writeback_val);
        }
        advancePC();
        return true;
    }

    if (bits27_25 == 7 && ((inst >> 20) & 0xF) == 0xF) {
        u32 svc_num = imm24(inst);
        advancePC();
        const u32 saved_pc = state_.r[15];
        const u32 saved_lr = state_.r[14];
        if (svc_handler_) {
            const bool cont = svc_handler_(svc_num);
            state_.r[15] = saved_pc;
            state_.r[14] = saved_lr;
            return cont;
        }
        return true;
    }

    if (bits27_25 == 0 || bits27_25 == 1) {
        const u32 op1 = opcode1(inst);

        // BX / BLX (register): 0x012FFF10 = BX Rm, 0x012FFF30 = BLX Rm (L=1). Target must be word-aligned (bit 0 clear).
        if ((inst & MASK_BLX_REG) == 0x012FFF10) {
            const bool is_blx = (inst & (1u << 5)) != 0;
            const u32 rm = Rm(inst);
            if (!is_blx && rm == 14) {
                Logger::log("BX LR: LR=0x%08X -> PC\n", state_.r[14]);
            }
            if (is_blx)
                writeReg(14, state_.r[15] + 4);
            state_.r[15] = state_.r[rm] & ~1u;
            return true;
        }

        if ((inst & MASK_NOP) == 0x01A00000) {
            advancePC();
            return true;
        }

        // SUB (and SUB with S): Rd = Rn - Op2; when S, C = no borrow = (Rn >= Op2). Do not write Rd for CMP (Rd==15).
        if ((inst & MASK_COND_OP1_S_Rd) == 0x00500000 && Rd(inst) != 15) {
            u32 rd = Rd(inst);
            u32 rn_val = state_.r[Rn(inst)];
            u32 op2_val = getOp2DataProcessing(*this, inst);
            u32 result = rn_val - op2_val;
            writeReg(rd, result);
            if (S(inst)) {
                bool z_flag = (result == 0u);
                setCpsrFlags(state_.cpsr,
                    (result & 0x80000000u) != 0, z_flag,
                    carryFromSub(rn_val, op2_val),
                    overflowFromSub(rn_val, op2_val, result));
            }
            advancePC();
            return true;
        }

        // CMP: SUBS with Rd=15. Only set flags; NEVER write to any register (not R0, not R15).
        if ((inst & MASK_COND_OP1_S_Rd) == 0x00A000F0) {
            u32 rn_val = state_.r[Rn(inst)];
            u32 op2_val = getOp2DataProcessing(*this, inst);
            u32 result = rn_val - op2_val;
            bool n_flag = (result & 0x80000000u) != 0;
            bool z_flag = (result == 0u);
            bool c_flag = carryFromSub(rn_val, op2_val);
            bool v_flag = overflowFromSub(rn_val, op2_val, result);
            setCpsrFlags(state_.cpsr, n_flag, z_flag, c_flag, v_flag);
            advancePC();
            return true;
        }

        // LDR (word) with immediate offset: same 12-bit offset, no expansion.
        if ((inst & MASK_LDRSTR) == 0x05800000) {
            u32 rd = Rd(inst);
            u32 rn = Rn(inst);
            u32 base = (rn == 15) ? (state_.r[15] + 8) : state_.r[rn];
            u32 offset12 = ldrStrOffset12(inst);
            u32 offset_val = U(inst) ? offset12 : (0u - offset12);
            u32 addr = P(inst) ? (base + offset_val) : base;
            u32 val = memory_.read32(addr);
            writeReg(rd, val);
            if (s_trace_enabled && rn == 15)
                Logger::log("  LDR [PC,#%s%u] addr=0x%08X -> 0x%08X (opcode 0x05800000 path)\n", U(inst) ? "" : "-", offset12, addr, val);
            if ((!P(inst) || W(inst)) && rn != 15) writeReg(rn, P(inst) ? addr : (base + offset_val));
            advancePC();
            return true;
        }

        // STR (word) with immediate offset.
        if ((inst & MASK_LDRSTR) == 0x05000000) {
            u32 rd = Rd(inst);
            u32 rn = Rn(inst);
            u32 base = (rn == 15) ? (state_.r[15] + 8) : state_.r[rn];
            u32 offset12 = ldrStrOffset12(inst);
            u32 offset_val = U(inst) ? offset12 : (0u - offset12);
            u32 addr = P(inst) ? (base + offset_val) : base;
            logStackRegionWrite(addr, state_.r[rd], state_.r[13], state_.r[15]);
            memory_.write32(addr, state_.r[rd]);
            if ((!P(inst) || W(inst)) && rn != 15) writeReg(rn, P(inst) ? addr : (base + offset_val));
            advancePC();
            return true;
        }

        // Data-processing (register form): Op2 from register + shift. S-bit means update flags (including CMP/TST/TEQ/CMN where Rd=15).
        if ((inst & MASK_DATAPROC) == 0x00000000 && !I(inst)) {
            u32 rd = Rd(inst);
            u32 rn_val = state_.r[Rn(inst)];
            u32 op2_val = getOp2DataProcessing(*this, inst);
            u32 result = 0;
            switch (op1) {
                case 0x0: result = rn_val & op2_val; break;
                case 0x1: result = rn_val ^ op2_val; break;
                case 0x2: result = rn_val + op2_val; break;
                case 0x3: result = rn_val + op2_val; break;
                case 0x4: result = rn_val + op2_val; break;
                case 0x5: result = rn_val - op2_val; break;
                case 0x8: result = rn_val & op2_val; break;
                case 0x9: result = rn_val ^ op2_val; break;
                case 0xA: result = rn_val - op2_val; break;
                case 0xB: result = rn_val + op2_val; break;  // CMN
                case 0xC: result = rn_val | op2_val; break;
                case 0xD: result = op2_val; break;
                case 0xE: result = rn_val & ~op2_val; break;
                default: break;
            }
            // CMP/CMN/TST/TEQ (op1 8,9,A,B) never write to any register; only set flags.
            const bool compare_only = (op1 == 0x8 || op1 == 0x9 || op1 == 0xA || op1 == 0xB);
            if (rd != 15 && !compare_only)
                writeReg(rd, result);
            if (S(inst)) {
                bool n_flag = (result & 0x80000000u) != 0;
                bool z_flag = (result == 0u);
                bool c = isSubtractionOpcode(op1) ? carryFromSub(rn_val, op2_val)
                         : isAdditionOpcode(op1)  ? carryFromAdd(rn_val, op2_val, result)
                                                  : ((state_.cpsr & CpsrFlags::C) != 0);
                bool v = isSubtractionOpcode(op1) ? overflowFromSub(rn_val, op2_val, result)
                         : isAdditionOpcode(op1)  ? overflowFromAdd(rn_val, op2_val, result)
                                                  : ((state_.cpsr & CpsrFlags::V) != 0);
                setCpsrFlags(state_.cpsr, n_flag, z_flag, c, v);
            }
            advancePC();
            return true;
        }

        // Data-processing (immediate form): Op2 = expanded imm12. S-bit means update flags (including CMP/CMN with Rd=15).
        if ((inst & MASK_DATAPROC_IMM) == 0x02000000 && I(inst)) {
            u32 rd = Rd(inst);
            u32 rn_val = state_.r[Rn(inst)];
            u32 op2_val = expandImm12(imm12(inst));
            u32 result = 0;
            switch (op1) {
                case 0x0: result = rn_val & op2_val; break;
                case 0x1: result = rn_val ^ op2_val; break;
                case 0x2: result = rn_val + op2_val; break;
                case 0x4: result = rn_val + op2_val; break;
                case 0x5: result = rn_val - op2_val; break;
                case 0xA: result = rn_val - op2_val; break;
                case 0xC: result = rn_val | op2_val; break;
                case 0xD: result = op2_val; break;
                case 0xE: result = rn_val & ~op2_val; break;
                case 0xF: result = ~op2_val; break;
                default: break;
            }
            const bool compare_only_imm = (op1 == 0x8 || op1 == 0x9 || op1 == 0xA || op1 == 0xB);
            if (rd != 15 && !compare_only_imm)
                writeReg(rd, result);
            if (S(inst)) {
                bool n_flag = (result & 0x80000000u) != 0;
                bool z_flag = (result == 0u);
                bool c = isSubtractionOpcode(op1) ? carryFromSub(rn_val, op2_val)
                         : isAdditionOpcode(op1)  ? carryFromAdd(rn_val, op2_val, result)
                                                  : ((state_.cpsr & CpsrFlags::C) != 0);
                bool v = isSubtractionOpcode(op1) ? overflowFromSub(rn_val, op2_val, result)
                         : isAdditionOpcode(op1)  ? overflowFromAdd(rn_val, op2_val, result)
                                                  : ((state_.cpsr & CpsrFlags::V) != 0);
                setCpsrFlags(state_.cpsr, n_flag, z_flag, c, v);
            }
            advancePC();
            return true;
        }

        if ((inst & MASK_MOV_OP) == VAL_MOV_OP && !I(inst) && !S(inst)) {
            u32 rd = Rd(inst);
            writeReg(rd, state_.r[Rm(inst)]);
            advancePC();
            return true;
        }
    }

    Logger::log("ARM: unknown opcode 0x%08X at PC 0x%08X\n", inst, state_.r[15]);
    advancePC();
    return true;
}
