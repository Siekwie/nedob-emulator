#include "interpreter.hpp"
#include "../3ds/memory.hpp"
#include "../../common/logger.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace {

// Off by default; set to true only when debugging (e.g. after SVC to trace next instructions).
static bool s_log_reg_writes = false;
static bool s_trace_enabled = false;
static int s_trace_remaining = -1;
static bool s_trace_done = false;

u32 parseU32Env(const char* name, u32 default_value) {
    const char* s = std::getenv(name);
    if (!s || !*s) return default_value;
    char* end = nullptr;
    unsigned long v = std::strtoul(s, &end, 0);  // base 0: accepts 0x... hex
    if (!end || *end != '\0') return default_value;
    return static_cast<u32>(v);
}

int parseIntEnv(const char* name, int default_value) {
    const char* s = std::getenv(name);
    if (!s || !*s) return default_value;
    char* end = nullptr;
    long v = std::strtol(s, &end, 0);
    if (!end || *end != '\0') return default_value;
    if (v < 1) return default_value;
    if (v > 2000) v = 2000;  // keep logs bounded
    return static_cast<int>(v);
}

void logRegWrite(u32 reg, u32 value, u32 pc) {
    // Register writes are frequent; only log during an active trace window.
    if (s_log_reg_writes && s_trace_remaining >= 0 && reg <= 14)
        Logger::log("  REG W R%u = 0x%08X  (PC=0x%08X)\n", reg, value, pc);
}

constexpr u32 STACK_TRACE_LO = 0x0F000000u;
constexpr u32 STACK_TRACE_HI = 0x10000000u;
constexpr u32 CPSR_T = (1u << 5);  // Thumb state bit

void logStackRegionWrite(u32 addr, u32 value, u32 sp, u32 pc) {
    // Stack writes are extremely frequent; only log during an active trace window.
    if (!(s_trace_enabled && s_trace_remaining >= 0)) return;
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
constexpr u32 MASK_NOP = 0x0FFFFFFFu;
constexpr u32 MASK_SVC = 0x0F000000u;   // SVC: bits 27-24 = 1111
constexpr u32 VAL_SVC = 0x0F000000u;

inline int32_t signExtend24(u32 v) {
    return static_cast<int32_t>((v & 0x800000) ? (v | 0xFF000000) : v);
}

// Trace: log next N instructions when PC hits either trigger.
constexpr u32 TRACE_TRIGGER_PC_DEFAULT = 0x00108A48u;
constexpr u32 TRACE_TRIGGER_PC_LOOP_DEFAULT = 0x00108A48u;
constexpr int TRACE_INSTRUCTION_COUNT_DEFAULT = 200;
static u32 s_trace_trigger_pc = TRACE_TRIGGER_PC_DEFAULT;
static u32 s_trace_trigger_pc_loop = TRACE_TRIGGER_PC_LOOP_DEFAULT;
static int s_trace_instruction_count = TRACE_INSTRUCTION_COUNT_DEFAULT;

void logArmDisasm(u32 pc, u32 inst, int index, bool is_trigger_pc,
                  const u32* r, u32 cpsr) {
    char mnem[80];
    const u32 bits27_25 = (inst >> 25) & 7;
    const u32 c = cond(inst);
    const u32 n = (cpsr >> 31) & 1u, z = (cpsr >> 30) & 1u, c_flag = (cpsr >> 29) & 1u, v = (cpsr >> 28) & 1u;

    if ((inst & MASK_SVC) == VAL_SVC) {
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
    } else if (((inst & 0x0FFFFFF0u) == 0x012FFF10u) || ((inst & 0x0FFFFFF0u) == 0x012FFF30u)) {
        const u32 rm = inst & 0xFu;
        const char* op = ((inst & 0x0FFFFFF0u) == 0x012FFF30u) ? "BLX" : "BX";
        std::snprintf(mnem, sizeof(mnem), "%s R%u", op, rm);
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
    const u32 shift = rot * 2;
    // ARM "modified immediate": 8-bit value rotated right by an even number of bits.
    // (rot==0 means no rotation)
    return (imm8 >> shift) | (imm8 << (32 - shift));
}

// Second operand for data-processing only. Do not use for LDR/STR offset.
u32 getOp2DataProcessing(ArmInterpreter& cpu, u32 inst) {
    if (I(inst)) {
        return expandImm12(imm12(inst));
    }
    const u32 rm = Rm(inst);
    // In ARM state, reads of R15 as an operand yield (PC + 8) due to the pipeline.
    u32 rm_val = (rm == 15) ? (cpu.state().r[15] + 8u) : cpu.state().r[rm];
    constexpr u32 SHIFT_TYPE_MASK = 3u;
    constexpr u32 SHIFT_IMM_MASK = 0x1Fu;
    u32 shift_type = (inst >> 5) & SHIFT_TYPE_MASK;
    u32 shift_imm = (inst >> 7) & SHIFT_IMM_MASK;
    switch (shift_type) {
        case 0: return shift_imm ? (rm_val << shift_imm) : rm_val;
        case 1: return shift_imm ? (rm_val >> shift_imm) : rm_val;
        case 2: return static_cast<u32>(static_cast<s32>(rm_val) >> (shift_imm ? shift_imm : 32));
        case 3:
            if (shift_imm == 0) {
                const u32 carry = (cpu.state().cpsr & CpsrFlags::C) ? 1u : 0u;
                return (carry << 31) | (rm_val >> 1);
            }
            return (rm_val >> shift_imm) | (rm_val << (32 - shift_imm));
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
    // Debug toggles; keep off by default to avoid multi-GB logs.
    if (const char* v = std::getenv("NEDOB_TRACE"); v && v[0] == '1') s_trace_enabled = true;
    if (const char* v = std::getenv("NEDOB_LOG_REG_WRITES"); v && v[0] == '1') s_log_reg_writes = true;
    if (s_trace_enabled) {
        s_trace_trigger_pc = parseU32Env("NEDOB_TRACE_TRIGGER_PC", TRACE_TRIGGER_PC_DEFAULT);
        s_trace_trigger_pc_loop = parseU32Env("NEDOB_TRACE_TRIGGER_PC_LOOP", TRACE_TRIGGER_PC_LOOP_DEFAULT);
        s_trace_instruction_count = parseIntEnv("NEDOB_TRACE_COUNT", TRACE_INSTRUCTION_COUNT_DEFAULT);
    }
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
    // Feed the memory system with best-effort PC context so unmapped accesses can be attributed.
    memory_.setCurrentAccessPC(pc);

    // Bringup guard: stop immediately if we ever fetch from an unmapped PC.
    // This prevents multi-GB logs of "unmapped read16" when we jump to garbage.
    static u32 s_prev_pc_guard = 0;
    const u32 prev_pc_guard = s_prev_pc_guard;
    const bool is_thumb = (state_.cpsr & CPSR_T) != 0;
    const u32 fetch_size = is_thumb ? 2u : 4u;
    if (!memory_.isMapped(pc, fetch_size)) {
        Logger::log(
            "CPU: unmapped instruction fetch PC=0x%08X (T=%u) prevPC=0x%08X LR=0x%08X SP=0x%08X CPSR=0x%08X\n",
            pc, is_thumb ? 1u : 0u, prev_pc_guard, state_.r[14], state_.r[13], state_.cpsr);
        if (prev_pc_guard != 0 && memory_.isMapped(prev_pc_guard, 4)) {
            memory_.setCurrentAccessPC(prev_pc_guard);
            const u32 prev_inst = memory_.read32(prev_pc_guard);
            Logger::log("CPU: previous inst @0x%08X = 0x%08X\n", prev_pc_guard, prev_inst);
            memory_.setCurrentAccessPC(pc);
        }
        return false;
    }
    s_prev_pc_guard = pc;

    if (pc >= 0x0FFFF000u && pc < 0x10000000u) {
        Logger::log("Stack Execution Detected - Probable Crash  PC=0x%08X  LR(R14)=0x%08X  SP(R13)=0x%08X\n",
                    pc, state_.r[14], state_.r[13]);
        return false;
    }

    // Bringup helper: detect first entry into the known idle loop at 0x0010486C/0x00104870.
    static u32 s_prev_pc = 0;
    if ((pc == 0x0010486Cu || pc == 0x00104870u) &&
        !(s_prev_pc == 0x0010486Cu || s_prev_pc == 0x00104870u)) {
        Logger::log("Entered idle loop at PC=0x%08X from PC=0x%08X (LR=0x%08X SP=0x%08X)\n",
                    pc, s_prev_pc, state_.r[14], state_.r[13]);
    }
    s_prev_pc = pc;

    // Bringup helper: break the known ARM atomic wait loop reached during early Pokemon Sun boot.
    // In a single-thread/no-scheduler environment this loop can spin forever waiting for a value
    // another thread would normally publish. We emulate forward progress by forcing the polled word
    // negative after repeated spins so the wait path can continue.
    static bool s_mutex_wait_patch_init = false;
    static bool s_mutex_wait_patch_enabled = true;
    static u32 s_mutex_wait_prev_addr = 0;
    static u32 s_mutex_wait_spin_count = 0;
    if (!s_mutex_wait_patch_init) {
        s_mutex_wait_patch_init = true;
        if (const char* v = std::getenv("NEDOB_PATCH_MUTEX_WAIT"); v && v[0] == '0') {
            s_mutex_wait_patch_enabled = false;
        }
    }
    if (s_mutex_wait_patch_enabled) {
        const bool in_mutex_wait_loop =
            (pc == 0x00108C2Cu || pc == 0x00108C30u || pc == 0x00108C34u || pc == 0x00108C38u ||
             pc == 0x00108C54u || pc == 0x00108C58u || pc == 0x00108C5Cu || pc == 0x00108C60u ||
             pc == 0x00108C64u || pc == 0x00108C80u || pc == 0x00108C84u);
        if (in_mutex_wait_loop) {
            const u32 addr = state_.r[0];
            if (addr == s_mutex_wait_prev_addr) {
                ++s_mutex_wait_spin_count;
            } else {
                s_mutex_wait_prev_addr = addr;
                s_mutex_wait_spin_count = 1;
            }
            if (s_mutex_wait_spin_count == 4096u && memory_.isMapped(addr, 4u)) {
                static bool s_logged_once = false;
                if (!s_logged_once) {
                    s_logged_once = true;
                    Logger::log("CPU: patching mutex wait word at 0x%08X after spin loop at PC=0x%08X\n", addr, pc);
                }
                memory_.write32(addr, 0xFFFFFFFFu);
            }
        } else {
            s_mutex_wait_spin_count = 0;
            s_mutex_wait_prev_addr = 0;
        }
    }

    // Thumb state: minimal Thumb-16 support (enough to get through common veneers/trampolines).
    // The 3DS userland binaries do use interworking, so we must honor CPSR.T on BX/BLX.
    if ((state_.cpsr & CPSR_T) != 0) {
        const u16 inst16 = memory_.read16(pc);

        // Thumb-2 32-bit BL (immediate): first halfword 11110 S imm10, second halfword 11111 J1 J2 imm11.
        // Also handle BLX (immediate), which switches to ARM state.
        if ((inst16 & 0xF800u) == 0xF000u) {
            const u16 inst16b = memory_.read16(pc + 2u);
            if ((inst16b & 0xF800u) == 0xF800u) {
                const u32 s = (inst16 >> 10) & 1u;
                const u32 imm10 = inst16 & 0x03FFu;
                const u32 j1 = (inst16b >> 13) & 1u;
                const u32 j2 = (inst16b >> 11) & 1u;
                const u32 imm11 = inst16b & 0x07FFu;
                const u32 i1 = (~(j1 ^ s)) & 1u;
                const u32 i2 = (~(j2 ^ s)) & 1u;
                u32 off = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
                // Sign-extend from bit24.
                if (s) off |= 0xFF000000u;
                const u32 next = pc + 4u;
                state_.r[14] = next | 1u;
                state_.r[15] = (next + off) & ~1u;
                return true;
            }
            if ((inst16b & 0xF800u) == 0xE800u) {
                // BLX (immediate): imm = S:I1:I2:imm10:imm10H:00
                const u32 s = (inst16 >> 10) & 1u;
                const u32 imm10 = inst16 & 0x03FFu;
                const u32 j1 = (inst16b >> 13) & 1u;
                const u32 j2 = (inst16b >> 11) & 1u;
                const u32 imm10h = (inst16b >> 1) & 0x03FFu;
                const u32 i1 = (~(j1 ^ s)) & 1u;
                const u32 i2 = (~(j2 ^ s)) & 1u;
                u32 off = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm10h << 2);
                if (s) off |= 0xFF000000u;
                const u32 next = pc + 4u;
                state_.r[14] = next | 1u;
                state_.cpsr &= ~CPSR_T;
                state_.r[15] = (next + off) & ~3u;
                return true;
            }
        }

        // Shift by immediate (Thumb-1): 000 op imm5 Rm Rd
        // op: 00=LSL, 01=LSR, 10=ASR
        if ((inst16 & 0xE000u) == 0x0000u) {
            const u32 op = (inst16 >> 11) & 0x3u;
            const u32 imm5 = (inst16 >> 6) & 0x1Fu;
            const u32 rm = (inst16 >> 3) & 0x7u;
            const u32 rd = inst16 & 0x7u;
            const u32 val = state_.r[rm];
            const bool old_v = (state_.cpsr & CpsrFlags::V) != 0;
            const bool old_c = (state_.cpsr & CpsrFlags::C) != 0;
            u32 result = val;
            bool carry = old_c;
            if (op == 0) {
                // LSL
                if (imm5 != 0) {
                    carry = ((val >> (32u - imm5)) & 1u) != 0;
                    result = val << imm5;
                }
            } else if (op == 1) {
                // LSR (imm5==0 => shift by 32)
                const u32 shift = (imm5 == 0) ? 32u : imm5;
                carry = ((val >> (shift - 1u)) & 1u) != 0;
                result = (shift == 32u) ? 0u : (val >> shift);
            } else if (op == 2) {
                // ASR (imm5==0 => shift by 32)
                const u32 shift = (imm5 == 0) ? 32u : imm5;
                carry = ((val >> (shift - 1u)) & 1u) != 0;
                result = static_cast<u32>(static_cast<s32>(val) >> (shift == 32u ? 31u : shift));
            } else {
                // op==3 is "add/sub" group; let other decoders handle it.
                goto thumb_unknown;
            }
            state_.r[rd] = result;
            const bool n = (result & 0x80000000u) != 0;
            const bool z = (result == 0);
            setCpsrFlags(state_.cpsr, n, z, carry, old_v);
            state_.r[15] = pc + 2u;
            return true;
        }

        // ALU operations (Thumb-1): 010000 op Rs Rd
        if ((inst16 & 0xFC00u) == 0x4000u) {
            const u32 op = (inst16 >> 6) & 0xFu;
            const u32 rs = (inst16 >> 3) & 0x7u;
            const u32 rd = inst16 & 0x7u;
            const u32 a = state_.r[rd];
            const u32 b = state_.r[rs];
            const bool old_c = (state_.cpsr & CpsrFlags::C) != 0;
            const bool old_v = (state_.cpsr & CpsrFlags::V) != 0;

            auto set_nz_keepcv = [&](u32 r) {
                const bool n = (r & 0x80000000u) != 0;
                const bool z = (r == 0);
                setCpsrFlags(state_.cpsr, n, z, old_c, old_v);
            };

            auto set_nzcv = [&](u32 r, bool c, bool v) {
                const bool n = (r & 0x80000000u) != 0;
                const bool z = (r == 0);
                setCpsrFlags(state_.cpsr, n, z, c, v);
            };

            switch (op) {
                case 0x0: { // AND
                    const u32 r = a & b;
                    state_.r[rd] = r;
                    set_nz_keepcv(r);
                    break;
                }
                case 0x1: { // EOR
                    const u32 r = a ^ b;
                    state_.r[rd] = r;
                    set_nz_keepcv(r);
                    break;
                }
                case 0x2: { // LSL (register)
                    const u32 shift = b & 0xFFu;
                    u32 r = a;
                    bool c = old_c;
                    if (shift != 0) {
                        if (shift < 32u) {
                            c = ((a >> (32u - shift)) & 1u) != 0;
                            r = a << shift;
                        } else if (shift == 32u) {
                            c = (a & 1u) != 0;
                            r = 0;
                        } else {
                            c = false;
                            r = 0;
                        }
                    }
                    state_.r[rd] = r;
                    set_nzcv(r, c, old_v);
                    break;
                }
                case 0x3: { // LSR (register)
                    const u32 shift = b & 0xFFu;
                    u32 r = a;
                    bool c = old_c;
                    if (shift != 0) {
                        if (shift < 32u) {
                            c = ((a >> (shift - 1u)) & 1u) != 0;
                            r = a >> shift;
                        } else if (shift == 32u) {
                            c = (a >> 31) != 0;
                            r = 0;
                        } else {
                            c = false;
                            r = 0;
                        }
                    }
                    state_.r[rd] = r;
                    set_nzcv(r, c, old_v);
                    break;
                }
                case 0x4: { // ASR (register)
                    const u32 shift = b & 0xFFu;
                    u32 r = a;
                    bool c = old_c;
                    if (shift != 0) {
                        if (shift < 32u) {
                            c = ((a >> (shift - 1u)) & 1u) != 0;
                            r = static_cast<u32>(static_cast<s32>(a) >> shift);
                        } else {
                            // shift >= 32: all bits become sign bit
                            c = (a >> 31) != 0;
                            r = c ? 0xFFFFFFFFu : 0u;
                        }
                    }
                    state_.r[rd] = r;
                    set_nzcv(r, c, old_v);
                    break;
                }
                case 0x5: { // ADC
                    const u32 carry_in = old_c ? 1u : 0u;
                    const u64 sum = static_cast<u64>(a) + static_cast<u64>(b) + carry_in;
                    const u32 r = static_cast<u32>(sum);
                    const bool c = (sum >> 32) != 0;
                    const bool v = overflowFromAdd(a, b + carry_in, r);
                    state_.r[rd] = r;
                    set_nzcv(r, c, v);
                    break;
                }
                case 0x6: { // SBC: a - b - (1-C)
                    const u32 borrow = old_c ? 0u : 1u;
                    const u64 sub = static_cast<u64>(a) - static_cast<u64>(b) - borrow;
                    const u32 r = static_cast<u32>(sub);
                    const bool c = static_cast<u64>(a) >= (static_cast<u64>(b) + borrow);
                    const bool v = overflowFromSub(a, b + borrow, r);
                    state_.r[rd] = r;
                    set_nzcv(r, c, v);
                    break;
                }
                case 0x7: { // ROR (register)
                    const u32 shift = b & 0xFFu;
                    u32 r = a;
                    bool c = old_c;
                    if (shift != 0) {
                        const u32 s = shift & 31u;
                        if (s == 0) {
                            r = a;
                            c = (a >> 31) != 0;
                        } else {
                            r = (a >> s) | (a << (32u - s));
                            c = (r >> 31) != 0;
                        }
                    }
                    state_.r[rd] = r;
                    set_nzcv(r, c, old_v);
                    break;
                }
                case 0x8: { // TST
                    const u32 r = a & b;
                    set_nz_keepcv(r);
                    break;
                }
                case 0x9: { // NEG: 0 - b
                    const u32 r = 0u - b;
                    const bool c = (b == 0);
                    const bool v = overflowFromSub(0u, b, r);
                    state_.r[rd] = r;
                    set_nzcv(r, c, v);
                    break;
                }
                case 0xA: { // CMP
                    const u32 r = a - b;
                    const bool c = a >= b;
                    const bool v = overflowFromSub(a, b, r);
                    set_nzcv(r, c, v);
                    break;
                }
                case 0xB: { // CMN
                    const u32 r = a + b;
                    const bool c = carryFromAdd(a, b, r);
                    const bool v = overflowFromAdd(a, b, r);
                    set_nzcv(r, c, v);
                    break;
                }
                case 0xC: { // ORR
                    const u32 r = a | b;
                    state_.r[rd] = r;
                    set_nz_keepcv(r);
                    break;
                }
                case 0xD: { // MUL
                    const u32 r = a * b;
                    state_.r[rd] = r;
                    // C/V are architecturally "unpredictable" here; keep them stable.
                    set_nz_keepcv(r);
                    break;
                }
                case 0xE: { // BIC
                    const u32 r = a & ~b;
                    state_.r[rd] = r;
                    set_nz_keepcv(r);
                    break;
                }
                case 0xF: { // MVN
                    const u32 r = ~b;
                    state_.r[rd] = r;
                    set_nz_keepcv(r);
                    break;
                }
                default:
                    goto thumb_unknown;
            }

            state_.r[15] = pc + 2u;
            return true;
        }

        // MOV/CMP/ADD/SUB (immediate, Thumb-1):
        // 00100 Rd imm8  (MOVS)
        // 00101 Rn imm8  (CMP)
        // 00110 Rd imm8  (ADDS)
        // 00111 Rd imm8  (SUBS)
        if ((inst16 & 0xF800u) == 0x2000u || (inst16 & 0xF800u) == 0x2800u ||
            (inst16 & 0xF800u) == 0x3000u || (inst16 & 0xF800u) == 0x3800u) {
            const u32 op = (inst16 >> 11) & 0x3u;
            const u32 rd = (inst16 >> 8) & 0x7u;
            const u32 imm8 = inst16 & 0xFFu;
            const u32 a = state_.r[rd];
            const bool old_c = (state_.cpsr & CpsrFlags::C) != 0;
            const bool old_v = (state_.cpsr & CpsrFlags::V) != 0;
            auto set_nz_keepcv = [&](u32 r) {
                const bool n = (r & 0x80000000u) != 0;
                const bool z = (r == 0);
                setCpsrFlags(state_.cpsr, n, z, old_c, old_v);
            };
            auto set_nzcv = [&](u32 r, bool c, bool v) {
                const bool n = (r & 0x80000000u) != 0;
                const bool z = (r == 0);
                setCpsrFlags(state_.cpsr, n, z, c, v);
            };
            if (op == 0) { // MOVS
                state_.r[rd] = imm8;
                set_nz_keepcv(imm8);
            } else if (op == 1) { // CMP
                const u32 r = a - imm8;
                const bool c = a >= imm8;
                const bool v = overflowFromSub(a, imm8, r);
                set_nzcv(r, c, v);
            } else if (op == 2) { // ADDS
                const u32 r = a + imm8;
                const bool c = carryFromAdd(a, imm8, r);
                const bool v = overflowFromAdd(a, imm8, r);
                state_.r[rd] = r;
                set_nzcv(r, c, v);
            } else { // SUBS
                const u32 r = a - imm8;
                const bool c = a >= imm8;
                const bool v = overflowFromSub(a, imm8, r);
                state_.r[rd] = r;
                set_nzcv(r, c, v);
            }
            state_.r[15] = pc + 2u;
            return true;
        }

        // LDR/STR (immediate, Thumb-1): 011x / 1000 groups.
        // 0110: STR/LDR (word, imm5<<2), 0111: STRB/LDRB (byte, imm5)
        // 1000: STRH/LDRH (halfword, imm5<<1)
        if ((inst16 & 0xE000u) == 0x6000u) {
            const u32 op = (inst16 >> 11) & 0x3u; // 00 STR, 01 LDR, 10 STRB, 11 LDRB
            const u32 imm5 = (inst16 >> 6) & 0x1Fu;
            const u32 rn = (inst16 >> 3) & 0x7u;
            const u32 rt = inst16 & 0x7u;
            const u32 base = state_.r[rn];
            const u32 offset = (op < 2u) ? (imm5 << 2) : imm5;
            const u32 addr = base + offset;
            if (op == 0) {
                memory_.write32(addr, state_.r[rt]);
            } else if (op == 1) {
                state_.r[rt] = memory_.read32(addr);
            } else if (op == 2) {
                memory_.write8(addr, static_cast<u8>(state_.r[rt] & 0xFFu));
            } else {
                state_.r[rt] = memory_.read8(addr);
            }
            state_.r[15] = pc + 2u;
            return true;
        }
        if ((inst16 & 0xF000u) == 0x8000u) {
            const bool load = (inst16 & 0x0800u) != 0;
            const u32 imm5 = (inst16 >> 6) & 0x1Fu;
            const u32 rn = (inst16 >> 3) & 0x7u;
            const u32 rt = inst16 & 0x7u;
            const u32 addr = state_.r[rn] + (imm5 << 1);
            if (load) state_.r[rt] = memory_.read16(addr);
            else memory_.write16(addr, static_cast<u16>(state_.r[rt] & 0xFFFFu));
            state_.r[15] = pc + 2u;
            return true;
        }

        // BX/BLX (register): 010001 11 H:Rm 000
        // Encoding for BX Rm is 0x4700 | (Rm << 3). BLX is 0x4780 | (Rm << 3).
        if ((inst16 & 0xFF00u) == 0x4700u) {
            const bool is_blx = (inst16 & 0x0080u) != 0;
            const u32 rm = (inst16 >> 3) & 0xFu;
            const u32 target = state_.r[rm];
            if (is_blx) {
                // In Thumb state, LR gets the address of the next instruction with bit0 set.
                state_.r[14] = (pc + 2u) | 1u;
            }
            if ((target & 1u) != 0) state_.cpsr |= CPSR_T;
            else state_.cpsr &= ~CPSR_T;
            state_.r[15] = target & ~1u;
            return true;
        }

        // PUSH/POP (Thumb-1): 1011 0 10 M reglist (PUSH) / 1011 1 10 P reglist (POP)
        // PUSH uses SP and optionally LR; POP optionally loads PC.
        if ((inst16 & 0xFE00u) == 0xB400u) {
            const bool include_lr = (inst16 & 0x0100u) != 0;
            const u32 reglist = inst16 & 0x00FFu;
            int count = 0;
            for (u32 i = 0; i < 8; ++i) if (reglist & (1u << i)) ++count;
            if (include_lr) ++count;
            if (count == 0) {
                state_.r[15] = pc + 2u;
                return true;
            }
            u32 sp = state_.r[13];
            u32 addr = sp - 4u * static_cast<u32>(count);
            u32 cur = addr;
            for (u32 i = 0; i < 8; ++i) {
                if (!(reglist & (1u << i))) continue;
                logStackRegionWrite(cur, state_.r[i], state_.r[13], state_.r[15]);
                memory_.write32(cur, state_.r[i]);
                cur += 4;
            }
            if (include_lr) {
                logStackRegionWrite(cur, state_.r[14], state_.r[13], state_.r[15]);
                memory_.write32(cur, state_.r[14]);
                cur += 4;
            }
            state_.r[13] = addr;
            state_.r[15] = pc + 2u;
            return true;
        }
        if ((inst16 & 0xFE00u) == 0xBC00u) {
            const bool include_pc = (inst16 & 0x0100u) != 0;
            const u32 reglist = inst16 & 0x00FFu;
            int count = 0;
            for (u32 i = 0; i < 8; ++i) if (reglist & (1u << i)) ++count;
            if (include_pc) ++count;
            if (count == 0) {
                state_.r[15] = pc + 2u;
                return true;
            }
            u32 sp = state_.r[13];
            u32 addr = sp;
            for (u32 i = 0; i < 8; ++i) {
                if (!(reglist & (1u << i))) continue;
                state_.r[i] = memory_.read32(addr);
                addr += 4;
            }
            if (include_pc) {
                const u32 value = memory_.read32(addr);
                addr += 4;
                if ((value & 1u) != 0) state_.cpsr |= CPSR_T;
                else state_.cpsr &= ~CPSR_T;
                state_.r[15] = value & ~1u;
            } else {
                state_.r[15] = pc + 2u;
            }
            state_.r[13] = sp + 4u * static_cast<u32>(count);
            return true;
        }

        // STMIA/LDMIA (Thumb-1): 1100 L Rn reglist
        if ((inst16 & 0xF000u) == 0xC000u) {
            const bool load = (inst16 & 0x0800u) != 0;
            const u32 rn = (inst16 >> 8) & 0x7u;
            const u32 reglist = inst16 & 0x00FFu;
            u32 addr = state_.r[rn];
            int count = 0;
            for (u32 i = 0; i < 8; ++i) if (reglist & (1u << i)) ++count;
            if (count == 0) {
                state_.r[15] = pc + 2u;
                return true;
            }
            if (load) {
                for (u32 i = 0; i < 8; ++i) {
                    if (!(reglist & (1u << i))) continue;
                    state_.r[i] = memory_.read32(addr);
                    addr += 4;
                }
            } else {
                for (u32 i = 0; i < 8; ++i) {
                    if (!(reglist & (1u << i))) continue;
                    logStackRegionWrite(addr, state_.r[i], state_.r[13], state_.r[15]);
                    memory_.write32(addr, state_.r[i]);
                    addr += 4;
                }
            }
            state_.r[rn] = state_.r[rn] + 4u * static_cast<u32>(count);
            state_.r[15] = pc + 2u;
            return true;
        }

        // B (unconditional, Thumb-1): 11100 imm11
        if ((inst16 & 0xF800u) == 0xE000u) {
            const u32 imm11 = inst16 & 0x07FFu;
            // Sign-extend imm11<<1 from 12 bits.
            u32 off = imm11 << 1;
            if (off & 0x800u) off |= 0xFFFFF000u;
            state_.r[15] = pc + 4u + off;
            return true;
        }

        // B<cond> (Thumb-1): 1101 cond imm8 (cond != 0xF)
        if ((inst16 & 0xF000u) == 0xD000u && (inst16 & 0x0F00u) != 0x0F00u) {
            const u32 cond4 = (inst16 >> 8) & 0xFu;
            const u32 imm8 = inst16 & 0xFFu;
            u32 off = imm8 << 1;
            if (off & 0x100u) off |= 0xFFFFFE00u;  // sign extend from 9 bits
            if (conditionPassed(cond4, state_.cpsr)) state_.r[15] = pc + 4u + off;
            else state_.r[15] = pc + 2u;
            return true;
        }

        // SVC (Thumb): 1101 1111 imm8
        if ((inst16 & 0xFF00u) == 0xDF00u) {
            const u32 svc_num = inst16 & 0xFFu;
            const u32 saved_pc = pc + 2u;
            const u32 saved_lr = state_.r[14];
            if (svc_handler_) {
                const bool cont = svc_handler_(svc_num);
                if (!cont) {
                    state_.r[15] = pc;
                    return false;
                }
            } else {
                Logger::log("SVC 0x%02X (thumb) called but no handler\n", svc_num);
            }
            state_.r[15] = saved_pc;
            state_.r[14] = saved_lr;
            return true;
        }

thumb_unknown:
        Logger::log("THUMB: unknown opcode 0x%04X at PC 0x%08X\n", inst16, pc);
        return false;
    }

    const u32 inst = fetch32();
    const u32 cond_bits = cond(inst);

    if (s_trace_enabled && !s_trace_done &&
        (pc == s_trace_trigger_pc || pc == s_trace_trigger_pc_loop) && s_trace_remaining < 0) {
        s_trace_remaining = s_trace_instruction_count;
        s_trace_done = true;
        Logger::log("=== Trace: next %d instructions. State shown is BEFORE each instruction runs. ===\n", s_trace_instruction_count);
    }
    if (s_trace_enabled && s_trace_remaining >= 0) {
        const int index = s_trace_instruction_count - s_trace_remaining;
        logArmDisasm(pc, inst, index, pc == s_trace_trigger_pc, state_.r, state_.cpsr);
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
            if (!cont) {
                // Make "stop" persistent: keep PC on the SVC so the next slice
                // immediately returns 0 cycles and the core can mark execution stopped.
                state_.r[15] = pc;
                state_.r[14] = saved_lr;
                return false;
            }
            state_.r[15] = saved_pc;
            state_.r[14] = saved_lr;
            return true;
        }
        return true;
    }

    // CLREX (ARMv7): clear exclusive monitor. Encoding: 0xF57FF01F (unconditional).
    if (inst == 0xF57FF01Fu) {
        exclusive_valid_ = false;
        advancePC();
        return true;
    }

    // ARM exclusive access pair (ldrex/strex), used by user-space locks and atomics.
    if ((inst & 0x0FF00FFFu) == 0x01900F9Fu) {
        const u32 rn = (inst >> 16) & 0xFu;
        const u32 rt = (inst >> 12) & 0xFu;
        const u32 addr = state_.r[rn];
        const u32 value = memory_.read32(addr);
        writeReg(rt, value);
        exclusive_addr_ = addr;
        exclusive_valid_ = true;
        advancePC();
        return true;
    }
    if ((inst & 0x0FF00FF0u) == 0x01800F90u) {
        const u32 rn = (inst >> 16) & 0xFu;
        const u32 rd = (inst >> 12) & 0xFu;   // status: 0 success, 1 failure
        const u32 rt = inst & 0xFu;           // value to store
        const u32 addr = state_.r[rn];
        if (exclusive_valid_ && exclusive_addr_ == addr) {
            memory_.write32(addr, state_.r[rt]);
            writeReg(rd, 0);
        } else {
            writeReg(rd, 1);
        }
        exclusive_valid_ = false;
        advancePC();
        return true;
    }

    const u32 bits27_25 = (inst >> 25) & 7;

    // LDM/STM (bits 27-25 = 100). In ARMv7, cond=15 (0xF) is often "unconditional" for this class; do not skip.
    const bool is_ldm_stm = (bits27_25 == 4);
    if (!is_ldm_stm && !conditionPassed(cond_bits, state_.cpsr)) {
        if (s_trace_enabled && s_trace_remaining >= 0 && bits27_25 == 5) {
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
        // Bringup helper: detect when userland falls into the "idle/panic" infinite loop.
        static bool s_idle_loop_reported = false;
        if (!s_idle_loop_reported && taken &&
            (target == 0x0010486Cu || target == 0x00104870u) &&
            (pc != 0x0010486Cu && pc != 0x00104870u)) {
            Logger::log("Entered idle loop: branch from PC=0x%08X to 0x%08X (LR=0x%08X)\n",
                        pc, target, state_.r[14]);
            s_idle_loop_reported = true;
        }
        if (s_trace_enabled && s_trace_remaining >= 0 && cond_b != COND_AL && cond_b != COND_NV)
            Logger::log("  B cond=0x%X %s -> 0x%08X  NZCV=%u%u%u%u\n", cond_b, taken ? "TAKEN" : "not taken", target, n, z, c, v);
        const bool is_bl = (inst & (1u << 24)) != 0;
        if (is_bl)
            writeReg(14, state_.r[15] + 4);
        state_.r[15] = target;
        return true;
    }

    // VMRS APSR_nzcv, FPSCR appears very early in userland startup.
    // Handle it before generic decoder paths to avoid misclassification.
    if ((inst & 0x0FFFFFF0u) == 0x0EF1FA10u) {
        state_.cpsr = (state_.cpsr & 0x0FFFFFFFu) | (state_.fpscr & 0xF0000000u);
        advancePC();
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
                if (s_trace_enabled && s_trace_remaining >= 0 && rn == 15)
                    Logger::log("  LDRB [PC,#%s%u] addr=0x%08X -> 0x%02X\n", U(inst) ? "" : "-", offset12, addr, val & 0xFFu);
            } else {
                u32 val = memory_.read32(addr);
                // Loads into PC can switch instruction set state (ARMv5+ behavior).
                // This matters for veneers and for `pop {..., pc}` sequences.
                if (rd == 15) {
                    if (s_trace_enabled && s_trace_remaining >= 0) {
                        Logger::log("LDR POP PC: loading value 0x%08X from addr 0x%08X into PC\n", val, addr);
                    }
                    if ((val & 1u) != 0) state_.cpsr |= CPSR_T;
                    else state_.cpsr &= ~CPSR_T;
                    state_.r[15] = val & ~1u;
                } else {
                    writeReg(rd, val);
                }
                if (s_trace_enabled && s_trace_remaining >= 0 && rn == 15)
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
        // If we loaded PC, we already branched (and possibly switched state).
        if (!(L(inst) && rd == 15)) {
            advancePC();
        }
        return true;
    }

    // LDR/STR register offset (bits 27-25 = 011). Commonly used by memcpy/memset loops.
    if (bits27_25 == 3) {
        const u32 rn = Rn(inst);
        const u32 rd = Rd(inst);
        const u32 rm = Rm(inst);
        const u32 base = (rn == 15) ? (state_.r[15] + 8) : state_.r[rn];

        const u32 shift_imm = (inst >> 7) & 0x1Fu;
        const u32 shift_type = (inst >> 5) & 0x3u; // 00 LSL, 01 LSR, 10 ASR, 11 ROR/RRX
        // In ARM state, reads of R15 as an operand yield (PC + 8) due to the pipeline.
        u32 off = (rm == 15) ? (state_.r[15] + 8u) : state_.r[rm];
        switch (shift_type) {
            case 0: // LSL
                off = (shift_imm == 0) ? off : (off << shift_imm);
                break;
            case 1: { // LSR (imm==0 => 32)
                const u32 s = (shift_imm == 0) ? 32u : shift_imm;
                off = (s == 32u) ? 0u : (off >> s);
                break;
            }
            case 2: { // ASR (imm==0 => 32)
                const u32 s = (shift_imm == 0) ? 32u : shift_imm;
                off = static_cast<u32>(static_cast<s32>(off) >> (s == 32u ? 31u : s));
                break;
            }
            case 3: // ROR (imm==0 => RRX)
                if (shift_imm == 0) {
                    const u32 c = ((state_.cpsr & CpsrFlags::C) != 0) ? 1u : 0u;
                    off = (c << 31) | (off >> 1);
                } else {
                    const u32 r = shift_imm & 31u;
                    off = (off >> r) | (off << ((32u - r) & 31u));
                }
                break;
        }

        const u32 offset_val = U(inst) ? off : (0u - off);
        const u32 addr = P(inst) ? (base + offset_val) : base;

        if (L(inst)) {
            if (B(inst)) {
                const u32 val = memory_.read8(addr);
                writeReg(rd, val);
            } else {
                const u32 val = memory_.read32(addr);
                if (rd == 15) {
                    if (s_trace_enabled && s_trace_remaining >= 0) {
                        Logger::log("LDR POP PC: loading value 0x%08X from addr 0x%08X into PC\n", val, addr);
                    }
                    if ((val & 1u) != 0) state_.cpsr |= CPSR_T;
                    else state_.cpsr &= ~CPSR_T;
                    state_.r[15] = val & ~1u;
                } else {
                    writeReg(rd, val);
                }
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

        const bool do_writeback = (!P(inst) || W(inst)) && (rn != 15);
        if (do_writeback) {
            writeReg(rn, P(inst) ? addr : (base + offset_val));
        }
        if (!(L(inst) && rd == 15)) {
            advancePC();
        }
        return true;
    }

    // MRC/MCR coprocessor register transfer (CP15 system + CP10/11 VFP system regs).
    if ((inst & 0x0F000010u) == 0x0E000010u) {
        const u32 coproc = (inst >> 8) & 0xFu;
        const bool is_mrc = ((inst >> 20) & 1u) != 0;
        const u32 rd = Rd(inst);
        const u32 crn = (inst >> 16) & 0xFu;
        const u32 crm = inst & 0xFu;
        const u32 opc1 = (inst >> 21) & 0x7u;
        const u32 opc2 = (inst >> 5) & 0x7u;
        if (coproc == 15u) {
            if (is_mrc) {
                u32 value = 0;
                // MRC p15,0,Rd,c13,c0,3: TPIDRURO (user thread pointer / TLS base).
                if (crn == 13u && crm == 0u && opc1 == 0u && opc2 == 3u) {
                    // Use the mapped TLS page (writable) rather than the system-info region.
                    value = STACK_TLS_VADDR;
                }
                writeReg(rd, value);
            }
            // For now, ignore CP15 writes (MCR) and return success.
            advancePC();
            return true;
        }
        if (coproc == 10u || coproc == 11u) {
            // VFP/NEON system register transfers show up early in crt0/libc.
            // We don't model VFP state yet; returning 0 and ignoring writes gets us past init code.
            if (is_mrc) {
                writeReg(rd, 0);
            }
            advancePC();
            return true;
        }
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
            // IA / IB
            addr = pre ? (base + 4) : base;
        } else {
            // DA / DB
            addr = pre
                ? (base - 4u * static_cast<u32>(count))                        // DB: first at base-4*count
                : (base - 4u * static_cast<u32>(count - 1));                   // DA: first at base-4*(count-1)
        }
        const u32 writeback_val = up ? (base + 4u * static_cast<u32>(count)) : (base - 4u * static_cast<u32>(count));
        const bool loads_pc = load && ((list & (1u << 15)) != 0);

        if (load) {
            for (u32 i = 0; i < 16; ++i) {
                if (!(list & (1u << i))) continue;
                const u32 value = memory_.read32(addr);
                if (i == 15) {
                    if (s_trace_enabled && s_trace_remaining >= 0) {
                        Logger::log("LDM POP PC: loading value 0x%08X from addr 0x%08X into PC\n", value, addr);
                    }
                    // Loading PC via LDM can switch instruction set state (bit0 indicates Thumb).
                    if ((value & 1u) != 0) state_.cpsr |= CPSR_T;
                    else state_.cpsr &= ~CPSR_T;
                    state_.r[15] = value & ~1u;
                } else {
                    writeReg(i, value);
                }
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
        if (!loads_pc) {
            advancePC();
        }
        return true;
    }

    // Minimal VFP (CP10) support needed by early userland startup:
    // - VLDR/VSTR (single)
    // - VCMP.F32
    // - VMRS APSR_nzcv, FPSCR
    if (bits27_25 == 6 || bits27_25 == 7) {
        // VLDR/VSTR (single): cond 110D U D 01 Rn Vd 1010 imm8
        // Addressing is +/- (imm8 << 2) from Rn (or PC+8 for literal loads).
        if ((inst & 0x0F300F00u) == 0x0D100A00u) {
            const bool load = (inst & (1u << 20)) != 0;
            const bool add = (inst & (1u << 23)) != 0;
            const u32 d = (inst >> 22) & 1u;
            const u32 vd = (inst >> 12) & 0xFu;
            const u32 sreg = (vd << 1) | d;
            const u32 rn = Rn(inst);
            const u32 base = (rn == 15u) ? ((state_.r[15] + 8u) & ~3u) : state_.r[rn];
            const u32 offset = (inst & 0xFFu) << 2;
            const u32 addr = add ? (base + offset) : (base - offset);
            if (load) state_.s[sreg] = memory_.read32(addr);
            else memory_.write32(addr, state_.s[sreg]);
            advancePC();
            return true;
        }

        // VCMP.F32 Sd, Sm
        if ((inst & 0x0FBF0FD0u) == 0x0EB40A40u) {
            const u32 d = (inst >> 22) & 1u;
            const u32 vd = (inst >> 12) & 0xFu;
            const u32 m = (inst >> 5) & 1u;
            const u32 vm = inst & 0xFu;
            const u32 sd = (vd << 1) | d;
            const u32 sm = (vm << 1) | m;

            float a = 0.0f, b = 0.0f;
            std::memcpy(&a, &state_.s[sd], sizeof(float));
            std::memcpy(&b, &state_.s[sm], sizeof(float));

            u32 nzcv = 0;
            if (std::isnan(a) || std::isnan(b)) {
                // Unordered: C=1, V=1
                nzcv = CpsrFlags::C | CpsrFlags::V;
            } else if (a == b) {
                nzcv = CpsrFlags::Z | CpsrFlags::C;
            } else if (a < b) {
                nzcv = CpsrFlags::N;
            } else {
                nzcv = CpsrFlags::C;
            }
            state_.fpscr = (state_.fpscr & 0x0FFFFFFFu) | nzcv;
            advancePC();
            return true;
        }

        // VMRS APSR_nzcv, FPSCR
        if ((inst & 0x0FFFFFF0u) == 0x0EF1FA10u) {
            state_.cpsr = (state_.cpsr & 0x0FFFFFFFu) | (state_.fpscr & 0xF0000000u);
            advancePC();
            return true;
        }
    }

    if (bits27_25 == 0 || bits27_25 == 1) {
        const u32 op1 = opcode1(inst);

        // BX / BLX (register): 0x012FFF10 = BX Rm, 0x012FFF30 = BLX Rm (L=1).
        const u32 bx_tag = (inst & 0x0FFFFFF0u);
        if (bx_tag == 0x012FFF10u || bx_tag == 0x012FFF30u) {
            const bool is_blx = (bx_tag == 0x012FFF30u);
            const u32 rm = inst & 0xFu;
            const u32 target = state_.r[rm];
            if (!is_blx && rm == 14) {
                // This can be extremely spammy in busy loops; only log during an active trace window.
                if (s_trace_enabled && s_trace_remaining >= 0) {
                    Logger::log("BX LR: PC=0x%08X LR=0x%08X -> 0x%08X%s\n",
                                state_.r[15], state_.r[14], target & ~1u, (target & 1u) ? " (thumb)" : "");
                }
            }
            if (is_blx) {
                writeReg(14, state_.r[15] + 4);
            }

            // Bringup escape hatch: if we call through a function pointer that is totally unmapped,
            // allow skipping the call instead of spamming unmapped fetches.
            // Enable with NEDOB_SKIP_UNMAPPED_CALLS=1.
            static bool s_skip_unmapped_calls_inited = false;
            static bool s_skip_unmapped_calls = false;
            static bool s_skip_nonexec_calls = false;
            if (!s_skip_unmapped_calls_inited) {
                s_skip_unmapped_calls_inited = true;
                if (const char* v = std::getenv("NEDOB_SKIP_UNMAPPED_CALLS"); v && v[0] == '1') {
                    s_skip_unmapped_calls = true;
                }
                if (const char* v = std::getenv("NEDOB_SKIP_NONEXEC_CALLS"); v && v[0] == '1') {
                    s_skip_nonexec_calls = true;
                }
            }

            const u32 target_addr = target & ~1u;
            const bool target_thumb = (target & 1u) != 0;
            const u32 target_fetch = target_thumb ? 2u : 4u;
            // Optional bringup quirk: this callback table entry points one word before
            // a real ARM routine in some traces.
            // NEDOB_PATCH_BLX_2E7310_REDIRECT: skip worker call, simulate immediate success.
            // 2=redirect into worker at +4; 1=skip entirely, return to caller with R0=1.
            if (target_addr == 0x002E7310u && state_.r[15] == 0x00104578u) {
                const char* v = std::getenv("NEDOB_PATCH_BLX_2E7310_REDIRECT");
                if (v && v[0] == '2') {
                    state_.r[14] = 0x0010457Cu;
                    state_.r[15] = 0x002E7314u;
                    state_.cpsr &= ~CPSR_T;
                    return true;
                }
                if (v && v[0] == '1') {
                    state_.r[0] = 1u;
                    state_.r[15] = 0x0010457Cu;
                    state_.cpsr &= ~CPSR_T;
                    return true;
                }
            }
            const auto is_known_null_callback_site = [this]() {
                const u32 pc = state_.r[15];
                return pc == 0x00108918u || pc == 0x00108944u || pc == 0x00108960u ||
                       pc == 0x00108988u || pc == 0x001089A0u ||
                       pc == 0x00105440u || pc == 0x00105454u;
            };
            if (s_skip_unmapped_calls && is_blx && !memory_.isMapped(target_addr, target_fetch)) {
                Logger::log("CPU: skipping unmapped BLX target=0x%08X from PC=0x%08X (LR=0x%08X)\n",
                            target_addr, state_.r[15], state_.r[14]);
                // For known Pokemon Sun callback sites, a null function pointer should behave like
                // a failed callback (non-zero), not success. This avoids falling into bad epilogues.
                if ((target_addr == 0u && is_known_null_callback_site()) ||
                    (target_addr == 0x005F7938u && state_.r[15] == 0x00105440u)) {
                    state_.r[0] = 1u;
                } else if (target_addr == 0u && state_.r[15] == 0x0010887Cu) {
                    state_.r[0] = 0u;
                } else if (target_addr == 0u &&
                           (state_.r[15] == 0x00104540u || state_.r[15] == 0x00104550u)) {
                    state_.r[0] = 0u;
                }
                const u32 ret = state_.r[14];
                if ((ret & 1u) != 0) state_.cpsr |= CPSR_T;
                else state_.cpsr &= ~CPSR_T;
                state_.r[15] = ret & ~1u;
                return true;
            }
            if (s_skip_nonexec_calls && is_blx && !memory_.isExecutable(target_addr, target_fetch)) {
                Logger::log("CPU: skipping non-exec BLX target=0x%08X from PC=0x%08X (LR=0x%08X)\n",
                            target_addr, state_.r[15], state_.r[14]);
                if ((target_addr == 0u && is_known_null_callback_site()) ||
                    (target_addr == 0x005F7938u && state_.r[15] == 0x00105440u)) {
                    state_.r[0] = 1u;
                } else if (target_addr == 0u && state_.r[15] == 0x0010887Cu) {
                    state_.r[0] = 0u;
                } else if (target_addr == 0u &&
                           (state_.r[15] == 0x00104540u || state_.r[15] == 0x00104550u)) {
                    state_.r[0] = 0u;
                }
                const u32 ret = state_.r[14];
                if ((ret & 1u) != 0) state_.cpsr |= CPSR_T;
                else state_.cpsr &= ~CPSR_T;
                state_.r[15] = ret & ~1u;
                return true;
            }

            if (target_thumb) state_.cpsr |= CPSR_T;
            else state_.cpsr &= ~CPSR_T;
            state_.r[15] = target_addr;
            return true;
        }

        if ((inst & MASK_NOP) == 0x01A00000) {
            advancePC();
            return true;
        }

        // ARMv6T2 wide-immediate moves.
        // MOVW Rd, #imm16 : Rd = imm16
        // MOVT Rd, #imm16 : Rd[31:16] = imm16, lower half unchanged.
        if ((inst & 0x0FF00000u) == 0x03000000u || (inst & 0x0FF00000u) == 0x03400000u) {
            const bool is_movt = (inst & 0x00400000u) != 0;
            const u32 rd = Rd(inst);
            const u32 imm4 = (inst >> 16) & 0xFu;
            const u32 imm12 = inst & 0xFFFu;
            const u32 imm16 = (imm4 << 12) | imm12;
            if (is_movt) {
                const u32 value = (state_.r[rd] & 0x0000FFFFu) | (imm16 << 16);
                writeReg(rd, value);
            } else {
                writeReg(rd, imm16);
            }
            advancePC();
            return true;
        }

        // ARMv6 exclusive accesses used by atomics:
        //   LDREX Rt, [Rn]
        //   STREX Rd, Rt, [Rn]
        // Single-core bringup model: STREX always succeeds (Rd=0) and writes immediately.
        if ((inst & 0x0FF00FF0u) == 0x01900F90u) {  // LDREX
            const u32 rt = Rd(inst);
            const u32 rn = Rn(inst);
            const u32 addr = (rn == 15) ? (pc + 8u) : state_.r[rn];
            const u32 val = memory_.read32(addr);
            writeReg(rt, val);
            advancePC();
            return true;
        }
        if ((inst & 0x0FF00FF0u) == 0x01800F90u) {  // STREX
            const u32 rn = Rn(inst);
            const u32 rd = Rd(inst);      // status result
            const u32 rt = Rm(inst);      // value source register (bits[3:0] in this encoding)
            const u32 addr = (rn == 15) ? (pc + 8u) : state_.r[rn];
            logStackRegionWrite(addr, state_.r[rt], state_.r[13], state_.r[15]);
            memory_.write32(addr, state_.r[rt]);
            writeReg(rd, 0u);             // success
            advancePC();
            return true;
        }

        // SUB (and SUB with S): Rd = Rn - Op2; when S, C = no borrow = (Rn >= Op2). Do not write Rd for CMP (Rd==15).
        if ((inst & MASK_COND_OP1_S_Rd) == 0x00500000 && Rd(inst) != 15) {
            u32 rd = Rd(inst);
            const u32 rn = Rn(inst);
            u32 rn_val = (rn == 15) ? (pc + 8u) : state_.r[rn];
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
            const u32 rn = Rn(inst);
            u32 rn_val = (rn == 15) ? (pc + 8u) : state_.r[rn];
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
            if (s_trace_enabled && s_trace_remaining >= 0 && rn == 15)
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
            const u32 rn = Rn(inst);
            u32 rn_val = (rn == 15) ? (pc + 8u) : state_.r[rn];
            u32 op2_val = getOp2DataProcessing(*this, inst);
            u32 result = 0;
            switch (op1) {
                case 0x0: result = rn_val & op2_val; break;
                case 0x1: result = rn_val ^ op2_val; break;
                case 0x2: result = rn_val - op2_val; break; // SUB
                case 0x3: result = op2_val - rn_val; break; // RSB
                case 0x4: result = rn_val + op2_val; break;
                case 0x5: result = rn_val + op2_val + ((state_.cpsr & CpsrFlags::C) ? 1u : 0u); break; // ADC
                case 0x6: result = rn_val - op2_val - ((state_.cpsr & CpsrFlags::C) ? 0u : 1u); break; // SBC
                case 0x7: result = op2_val - rn_val - ((state_.cpsr & CpsrFlags::C) ? 0u : 1u); break; // RSC
                case 0x8: result = rn_val & op2_val; break;
                case 0x9: result = rn_val ^ op2_val; break;
                case 0xA: result = rn_val - op2_val; break;
                case 0xB: result = rn_val + op2_val; break;  // CMN
                case 0xC: result = rn_val | op2_val; break;
                case 0xD: result = op2_val; break;
                case 0xE: result = rn_val & ~op2_val; break;
                case 0xF: result = ~op2_val; break;
                default: break;
            }
            // CMP/CMN/TST/TEQ (op1 8,9,A,B) never write to any register; only set flags.
            const bool compare_only = (op1 == 0x8 || op1 == 0x9 || op1 == 0xA || op1 == 0xB);
            if (rd != 15 && !compare_only)
                writeReg(rd, result);
            if (S(inst)) {
                bool n_flag = (result & 0x80000000u) != 0;
                bool z_flag = (result == 0u);
                const bool carry_in = (state_.cpsr & CpsrFlags::C) != 0;
                bool c = ((state_.cpsr & CpsrFlags::C) != 0);
                bool v = ((state_.cpsr & CpsrFlags::V) != 0);
                if (op1 == 0x2 || op1 == 0xA) { // SUB / CMP
                    c = carryFromSub(rn_val, op2_val);
                    v = overflowFromSub(rn_val, op2_val, result);
                } else if (op1 == 0x3) { // RSB
                    c = carryFromSub(op2_val, rn_val);
                    v = overflowFromSub(op2_val, rn_val, result);
                } else if (op1 == 0x4 || op1 == 0xB) { // ADD / CMN
                    c = carryFromAdd(rn_val, op2_val, result);
                    v = overflowFromAdd(rn_val, op2_val, result);
                } else if (op1 == 0x5) { // ADC
                    const u64 wide = static_cast<u64>(rn_val) + static_cast<u64>(op2_val) + (carry_in ? 1ull : 0ull);
                    c = (wide >> 32) != 0;
                    v = overflowFromAdd(rn_val, op2_val + (carry_in ? 1u : 0u), result);
                } else if (op1 == 0x6) { // SBC
                    const u64 subtrahend = static_cast<u64>(op2_val) + (carry_in ? 0ull : 1ull);
                    c = static_cast<u64>(rn_val) >= subtrahend;
                    v = overflowFromSub(rn_val, op2_val + (carry_in ? 0u : 1u), result);
                } else if (op1 == 0x7) { // RSC
                    const u64 subtrahend = static_cast<u64>(rn_val) + (carry_in ? 0ull : 1ull);
                    c = static_cast<u64>(op2_val) >= subtrahend;
                    v = overflowFromSub(op2_val, rn_val + (carry_in ? 0u : 1u), result);
                }
                setCpsrFlags(state_.cpsr, n_flag, z_flag, c, v);
            }
            advancePC();
            return true;
        }

        // Data-processing (immediate form): Op2 = expanded imm12. S-bit means update flags (including CMP/CMN with Rd=15).
        if ((inst & MASK_DATAPROC_IMM) == 0x02000000 && I(inst)) {
            u32 rd = Rd(inst);
            const u32 rn = Rn(inst);
            u32 rn_val = (rn == 15) ? (pc + 8u) : state_.r[rn];
            u32 op2_val = expandImm12(imm12(inst));
            u32 result = 0;
            switch (op1) {
                case 0x0: result = rn_val & op2_val; break;
                case 0x1: result = rn_val ^ op2_val; break;
                case 0x2: result = rn_val - op2_val; break; // SUB
                case 0x3: result = op2_val - rn_val; break; // RSB
                case 0x4: result = rn_val + op2_val; break;
                case 0x5: result = rn_val + op2_val + ((state_.cpsr & CpsrFlags::C) ? 1u : 0u); break; // ADC
                case 0x6: result = rn_val - op2_val - ((state_.cpsr & CpsrFlags::C) ? 0u : 1u); break; // SBC
                case 0x7: result = op2_val - rn_val - ((state_.cpsr & CpsrFlags::C) ? 0u : 1u); break; // RSC
                case 0xA: result = rn_val - op2_val; break;
                case 0xB: result = rn_val + op2_val; break; // CMN
                case 0xC: result = rn_val | op2_val; break;
                case 0xD: result = op2_val; break;
                case 0xE: result = rn_val & ~op2_val; break;
                case 0xF: result = ~op2_val; break;
                default: break;
            }
            if (op1 == 0xA && state_.r[15] == 0x00104564u && rn == 0u && op2_val == 0u && state_.r[0] == 0u) {
                if (const char* v = std::getenv("NEDOB_PATCH_WORKER_READY_CHECK"); v && v[0] == '1') {
                    result = 1u;
                }
            }
            const bool compare_only_imm = (op1 == 0x8 || op1 == 0x9 || op1 == 0xA || op1 == 0xB);
            if (rd != 15 && !compare_only_imm)
                writeReg(rd, result);
            if (S(inst)) {
                bool n_flag = (result & 0x80000000u) != 0;
                bool z_flag = (result == 0u);
                const bool carry_in = (state_.cpsr & CpsrFlags::C) != 0;
                bool c = ((state_.cpsr & CpsrFlags::C) != 0);
                bool v = ((state_.cpsr & CpsrFlags::V) != 0);
                if (op1 == 0x2 || op1 == 0xA) { // SUB / CMP
                    c = carryFromSub(rn_val, op2_val);
                    v = overflowFromSub(rn_val, op2_val, result);
                } else if (op1 == 0x3) { // RSB
                    c = carryFromSub(op2_val, rn_val);
                    v = overflowFromSub(op2_val, rn_val, result);
                } else if (op1 == 0x4 || op1 == 0xB) { // ADD / CMN
                    c = carryFromAdd(rn_val, op2_val, result);
                    v = overflowFromAdd(rn_val, op2_val, result);
                } else if (op1 == 0x5) { // ADC
                    const u64 wide = static_cast<u64>(rn_val) + static_cast<u64>(op2_val) + (carry_in ? 1ull : 0ull);
                    c = (wide >> 32) != 0;
                    v = overflowFromAdd(rn_val, op2_val + (carry_in ? 1u : 0u), result);
                } else if (op1 == 0x6) { // SBC
                    const u64 subtrahend = static_cast<u64>(op2_val) + (carry_in ? 0ull : 1ull);
                    c = static_cast<u64>(rn_val) >= subtrahend;
                    v = overflowFromSub(rn_val, op2_val + (carry_in ? 0u : 1u), result);
                } else if (op1 == 0x7) { // RSC
                    const u64 subtrahend = static_cast<u64>(rn_val) + (carry_in ? 0ull : 1ull);
                    c = static_cast<u64>(op2_val) >= subtrahend;
                    v = overflowFromSub(op2_val, rn_val + (carry_in ? 0u : 1u), result);
                }
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
