#include "svc.hpp"
#include "../arm/interpreter.hpp"
#include "../3ds/memory.hpp"
#include "../../common/logger.hpp"
#include <cstdlib>
#include <cstring>

namespace {

std::string readStringFromMemory(MemorySystem& mem, u32 ptr) {
    std::string s;
    for (std::size_t i = 0; i < 256; ++i) {
        char c = static_cast<char>(mem.read8(ptr + static_cast<u32>(i)));
        if (c == '\0') break;
        s += c;
    }
    return s;
}

// Minimal helpers for common 3DS ABI structs.
// 3DS `svcQueryMemory` writes:
//   MemoryInfo { u32 base; u32 size; u32 perm; u32 state; }  (16 bytes)
//   PageInfo   { u32 flags; }                               (4 bytes)
static void writeMemoryInfo(MemorySystem& mem, u32 out_meminfo, u32 base, u32 size, u32 perm, u32 state) {
    if (out_meminfo == 0) return;
    mem.write32(out_meminfo + 0, base);
    mem.write32(out_meminfo + 4, size);
    mem.write32(out_meminfo + 8, perm);
    mem.write32(out_meminfo + 12, state);
}

static void writePageInfo(MemorySystem& mem, u32 out_pageinfo, u32 flags) {
    if (out_pageinfo == 0) return;
    mem.write32(out_pageinfo, flags);
}

const char* getSvcName(u32 n) {
    static const char* names[] = {
        "Unknown",          "ControlMemory",    "QueryMemory",      "ExitProcess",
        "GetProcessAffinityMask", "SetProcessAffinityMask", "GetProcessIdealProcessor",
        "SetProcessIdealProcessor", "CreateThread", "ExitThread", "SleepThread",
        "GetThreadPriority", "SetThreadPriority", "GetThreadAffinityMask",
        "SetThreadAffinityMask", "GetThreadIdealProcessor", "SetThreadIdealProcessor",
        "GetCurrentProcessorNumber", "Run", "CreateMutex", "ReleaseMutex",
        "CreateSemaphore", "ReleaseSemaphore", "CreateEvent", "SignalEvent",
        "ClearEvent", "CreateTimer", "SetTimer", "CancelTimer", "ClearTimer",
        "CreateMemoryBlock", "MapMemoryBlock", "UnmapMemoryBlock",
        "CreateAddressArbiter", "ArbitrateAddress", "CloseHandle",
        "WaitSynchronization1", "WaitSynchronizationN", "SignalAndWait",
        "DuplicateHandle", "GetSystemTick", "GetHandleInfo", "GetSystemInfo",
        "GetProcessInfo", "GetThreadInfo", "ConnectToPort", nullptr, nullptr, nullptr, nullptr,
        "SendSyncRequest", "OpenProcess", "OpenThread", "GetProcessId",
        "GetProcessIdOfThread", "GetThreadId", "GetResourceLimit",
        "GetResourceLimitLimitValues", "GetResourceLimitCurrentValues", "GetThreadContext",
        "Break", "OutputDebugString",
    };
    if (n == 0x3C) return "Break";
    if (n < sizeof(names) / sizeof(names[0]) && names[n]) {
        return names[n];
    }
    return "Unknown";
}

}  // namespace

SvcDispatcher::SvcDispatcher() {
    for (u32 i = 0; i < kMaxSvc; ++i) {
        handlers_[i] = nullptr;
    }
}

u32 SvcDispatcher::allocHandle(const std::string& type) {
    u32 id = next_handle_id_++;
    handle_table_[id] = type;
    return id;
}

void SvcDispatcher::setHandler(u32 svc_num, SvcDispatchFn fn) {
    if (svc_num < kMaxSvc) {
        handlers_[svc_num] = std::move(fn);
    }
}

bool SvcDispatcher::call(u32 svc_num, ArmInterpreter& cpu) {
    const auto& r = cpu.state().r;
    // QueryMemory can be called in very tight probe loops; default to quiet unless explicitly enabled.
    const bool log_query_memory = (std::getenv("NEDOB_LOG_QUERYMEMORY") != nullptr);
    const bool log_arbitrate = (std::getenv("NEDOB_LOG_ARBITRATE") != nullptr);
    const bool suppress_spam = (svc_num == 0x02 && !log_query_memory) || (svc_num == 0x22 && !log_arbitrate);
    if (!suppress_spam) {
        Logger::log("SVC 0x%02X %s  R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X  PC=0x%08X\n",
                    svc_num, getSvcName(svc_num), r[0], r[1], r[2], r[3], cpu.getPC());
    }

    if (svc_num < kMaxSvc && handlers_[svc_num]) {
        return handlers_[svc_num](svc_num, cpu);
    }

    switch (svc_num) {
        case 0x01: {
            // ControlMemory is used very early by crt0/allocators.
            //
            // Different SDKs/wrappers pass args differently, but a common convention is:
            // - R0: operation
            // - R1: addr hint (0 == anywhere)
            // - R2: (unused or alignment/flags)
            // - R3: size
            // and the SVC returns:
            // - R0: result (0 success)
            // - R1: output address (chosen mapping base)
            //
            // For bringup, we don't model VM pages yet; process memory is a flat backing store.
            // We still need to return a sane out address in R1 so userland doesn't store junk
            // and later jump into unmapped space.
            static u32 s_bump = HEAP_VADDR;

            const u32 op = cpu.state().r[0];
            const u32 hint = cpu.state().r[1];
            const u32 size_in = cpu.state().r[3];

            auto align_up = [](u32 v, u32 a) { return (v + (a - 1u)) & ~(a - 1u); };

            u32 size = size_in;
            if (size == 0 || size > (16u * 1024u * 1024u)) {
                size = 0x1000u; // defensive default for garbage args during bringup
            }
            size = align_up(size, 0x1000u);

            u32 out = 0;
            const bool hint_usable = (hint >= HEAP_VADDR && hint < HEAP_VADDR_END);
            if (hint_usable && (hint % 0x1000u) == 0) {
                out = hint;
            } else {
                out = align_up(s_bump, 0x1000u);
            }

            // Advance bump pointer only for "allocate/commit"-like ops. We don't decode op here;
            // this is purely to keep progress deterministic.
            if (out == s_bump) {
                s_bump = align_up(out + size, 0x1000u);
                if (s_bump < HEAP_VADDR || s_bump >= HEAP_VADDR_END) {
                    s_bump = HEAP_VADDR;
                }
            }

            Logger::log("ControlMemory: op=0x%08X hint=0x%08X size=0x%08X -> out=0x%08X\n",
                        op, hint, size_in, out);
            cpu.state().r[0] = 0;
            cpu.state().r[1] = out;
            return true;
        }
        case 0x02: {
            // QueryMemory(MemoryInfo* out, u32* out_pageinfo, u32 addr)
            // A lot of userland (crt0/libc/rtld) expects these outputs to be initialized.
            const u32 out_meminfo = cpu.state().r[0];
            const u32 out_pageinfo = cpu.state().r[1];
            const u32 addr = cpu.state().r[2];

            // Permissions: bitmask like {R=1, W=2, X=4}. State is "committed" vs "free".
            constexpr u32 PERM_NONE = 0;
            constexpr u32 PERM_R = 1;
            constexpr u32 PERM_W = 2;
            constexpr u32 PERM_X = 4;
            constexpr u32 STATE_FREE = 0;
            constexpr u32 STATE_COMMITTED = 1;

            // Default: unmapped 4KB page.
            u32 base = addr & ~0xFFFu;
            u32 size = 0x1000u;
            u32 perm = PERM_NONE;
            u32 state = STATE_FREE;

            auto set_region = [&](u32 region_base, u32 region_end) {
                base = region_base;
                size = region_end - region_base;
                if (size == 0) size = 0x1000u;
            };

            // Mark our user regions as committed so allocators/probers behave. Return
            // sensible region sizes so code that does `addr += size` doesn't end up
            // walking a 4KB-at-a-time loop across gigabytes of space.
            if (addr >= STACK_TLS_VADDR && addr < STACK_TLS_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(STACK_TLS_VADDR, STACK_TLS_VADDR_END);
            } else if (addr >= STACK_REGION_VADDR && addr < STACK_REGION_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(STACK_REGION_VADDR, STACK_REGION_VADDR_END);
            } else if (addr >= SHARED_PAGE_VADDR && addr < SHARED_PAGE_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(SHARED_PAGE_VADDR, SHARED_PAGE_VADDR_END);
            } else if (addr >= SYSTEM_INFO_VADDR && addr < SYSTEM_INFO_VADDR_END) {
                perm = PERM_R;
                state = STATE_COMMITTED;
                set_region(SYSTEM_INFO_VADDR, SYSTEM_INFO_VADDR_END);
            } else if (addr >= IO_STUB_VADDR && addr < IO_STUB_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(IO_STUB_VADDR, IO_STUB_VADDR_END);
            } else if (addr >= IO_AREA_VADDR && addr < IO_AREA_VADDR_END) {
                // We currently stub IO reads as 0 and ignore writes, but report the region as present.
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(IO_AREA_VADDR, IO_AREA_VADDR_END);
            } else if (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(VRAM_VADDR, VRAM_VADDR_END);
            } else if (addr >= VRAM_MIRROR_VADDR && addr < VRAM_MIRROR_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(VRAM_MIRROR_VADDR, VRAM_MIRROR_VADDR_END);
            } else if (addr >= LINEAR_HEAP_VADDR && addr < LINEAR_HEAP_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(LINEAR_HEAP_VADDR, LINEAR_HEAP_VADDR_END);
            } else if (addr >= SHARED_MEMORY_VADDR && addr < SHARED_MEMORY_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(SHARED_MEMORY_VADDR, SHARED_MEMORY_VADDR_END);
            } else if (addr >= HEAP_VADDR && addr < HEAP_VADDR_END) {
                perm = PERM_R | PERM_W;
                state = STATE_COMMITTED;
                set_region(HEAP_VADDR, HEAP_VADDR_END);
            } else if (addr >= PROCESS_IMAGE_VADDR && addr < PROCESS_IMAGE_VADDR_END) {
                perm = PERM_R | PERM_X;
                state = STATE_COMMITTED;
                set_region(PROCESS_IMAGE_VADDR, PROCESS_IMAGE_VADDR_END);
            }

            // ABI: return MemoryInfo fields in registers and (optionally) mirror to out pointers.
            // Many libctru wrappers copy R1-R4/R5 into user buffers after the SVC returns.
            writeMemoryInfo(cpu.memory_, out_meminfo, base, size, perm, state);
            writePageInfo(cpu.memory_, out_pageinfo, 0);
            cpu.state().r[0] = 0;      // Result
            cpu.state().r[1] = base;   // MemoryInfo.base_addr
            cpu.state().r[2] = size;   // MemoryInfo.size
            cpu.state().r[3] = perm;   // MemoryInfo.perm
            cpu.state().r[4] = state;  // MemoryInfo.state
            cpu.state().r[5] = 0;      // PageInfo.flags
            return true;
        }
        case 0x38: {
            // GetResourceLimit: return a closeable handle.
            //
            // Returning a kernel pseudo-handle here (e.g. 0xFFFF8001) causes userland to later
            // call CloseHandle on it and hit an error path (svcBreak). Give it a normal handle
            // so CloseHandle succeeds and the title can proceed.
            const u32 h = allocHandle("ResourceLimit");
            cpu.state().r[0] = 0;
            cpu.state().r[1] = h;
            return true;
        }
        case 0x23: {
            // CloseHandle: pseudo-handles (> 0xFFFF0000) are no-ops; do not modify handle table.
            const u32 handle = cpu.state().r[0];
            if (handle != 0 && handle <= 0xFFFF0000u) handle_table_.erase(handle);
            cpu.state().r[0] = 0;
            return true;
        }
        case 0x22: {
            // ArbitrateAddress(Handle arbiter, u32 addr, u32 type, s32 value, s64 timeout_ns)
            // Minimal single-thread model:
            // emulate waiter wakeups by adjusting the arbitration word instead of blocking.
            // This keeps lock/futex-style userspace code progressing during bringup.
            const u32 addr = cpu.state().r[1];
            const u32 type = cpu.state().r[2];
            const s32 value = static_cast<s32>(cpu.state().r[3]);
            if (addr != 0 && cpu.memory_.isMapped(addr, 4u)) {
                s32 cur = static_cast<s32>(cpu.memory_.read32(addr));
                switch (type) {
                    case 0: { // SIGNAL
                        // In real kernel this wakes waiters; here we just move the word out of "waiting" range.
                        if (cur < 0) cur = 0;
                        break;
                    }
                    case 1: { // WAIT_IF_LESS_THAN
                        if (cur < value) cur = value;
                        break;
                    }
                    case 2: { // DECREMENT_AND_WAIT_IF_LESS_THAN
                        --cur;
                        if (cur < value) cur = value;
                        break;
                    }
                    case 3: // WAIT_IF_LESS_THAN_TIMEOUT
                    case 4: { // DECREMENT_AND_WAIT_IF_LESS_THAN_TIMEOUT
                        if (type == 4) --cur;
                        if (cur < value) cur = value;
                        break;
                    }
                    default:
                        break;
                }
                cpu.memory_.write32(addr, static_cast<u32>(cur));
            }
            cpu.state().r[0] = 0;
            return true;
        }
        case 0x19: {
            u32 id = allocHandle("Thread");
            cpu.state().r[0] = 0;
            cpu.state().r[1] = id;
            return true;
        }
        case 0x1F: {
            const u32 out_ptr = cpu.state().r[0];
            const u32 name_ptr = cpu.state().r[1];
            std::string name = readStringFromMemory(cpu.memory_, name_ptr);
            Logger::log("ConnectToService: %s\n", name.c_str());
            if (name == "srv:") {
                handle_table_[0x20] = "srv:";
                cpu.memory_.write32(out_ptr, 0x20u);
                cpu.state().r[0] = 0;
                return true;
            }
            u32 h = allocHandle(name);
            cpu.memory_.write32(out_ptr, h);
            cpu.state().r[0] = 0;
            return true;
        }
        case 0x39: // GetResourceLimitLimitValues
        case 0x3A: { // GetResourceLimitCurrentValues
            // 3DS ABI (as observed in this title's crt0/libc):
            //   R0 = resource_id_list (u32*), may be null (treat as one entry with ID 0)
            //   R1 = resource_limit_handle (pseudo-handle 0xFFFF8001)
            //   R2 = out_values (u64[count])
            //   R3 = count
            //
            // Important: output is an array of u64 values (8 bytes each), NOT pairs. Writing 16 bytes
            // per entry will clobber the caller's stack frame (including saved LR), causing bad returns.
            const bool want_limits = (svc_num == 0x39);
            const u32 ids_ptr = cpu.state().r[0];
            const u32 out_ptr = cpu.state().r[2];
            u32 count = cpu.state().r[3];
            if (count > 32u) count = 32u;

            constexpr u64 COMMIT_LIMIT = 0x08000000ULL;
            constexpr u64 COMMIT_CURRENT = 0x00000000ULL;
            constexpr u64 PRIORITY_LIMIT = 64ULL;
            constexpr u64 PRIORITY_CURRENT = 1ULL;
            (void)cpu.state().r[1]; // handle (pseudo)

            for (u32 i = 0; i < count; ++i) {
                u32 rid = 0;
                if (ids_ptr != 0) rid = cpu.memory_.read32(ids_ptr + i * 4u);
                u64 value = 0;
                if (rid == 0) value = want_limits ? COMMIT_LIMIT : COMMIT_CURRENT;
                else if (rid == 1) value = want_limits ? PRIORITY_LIMIT : PRIORITY_CURRENT;
                cpu.memory_.write64(out_ptr + i * 8u, value);
            }
            cpu.state().r[0] = 0;
            return true;
        }
        case 0x03:
            Logger::log("SVC ExitProcess\n");
            return false;
        case 0x09:
            Logger::log("SVC ExitThread\n");
            return false;
        case 0x17: {
            u32 id = allocHandle("Semaphore");
            cpu.state().r[0] = 0;
            cpu.state().r[1] = id;
            return true;
        }
        case 0x21: {
            // CreateAddressArbiter(Handle* out)
            // Used for userland futex-like waits/wakes (very early during runtime init).
            u32 id = allocHandle("AddressArbiter");
            cpu.state().r[0] = 0;
            cpu.state().r[1] = id;
            return true;
        }
        case 0x25:
            cpu.state().r[0] = 0;
            cpu.state().r[1] = 0x00000010u;  // GetThreadId: thread ID in R1 (3DS ABI), must be non-zero
            return true;
        case 0x28:
            cpu.state().r[0] = 0;
            return true;
        case 0x2A: {
            // GetSystemInfo(u64* out, u32 type, u32 param)
            // Minimal values to keep userland from branching into error paths.
            const u32 out_ptr = cpu.state().r[0];
            const u32 type = cpu.state().r[1];
            const u32 param = cpu.state().r[2];
            (void)param;

            // Most callers treat unknown types as 0. Provide a couple common ones.
            u64 value = 0;
            // 0x10000: system model (0 = Old 3DS). Keep default.
            if (type == 0x10000u) value = 0;
            // 0x20000: kernel version. Use a non-zero placeholder.
            if (type == 0x20000u) value = 0x0000000200000000ULL;

            if (out_ptr != 0) cpu.memory_.write64(out_ptr, value);
            cpu.state().r[0] = 0;
            return true;
        }
        case 0x2B: {
            // GetProcessInfo(u64* out, Handle process, u32 type)
            const u32 out_ptr = cpu.state().r[0];
            const u32 process_handle = cpu.state().r[1];
            const u32 type = cpu.state().r[2];
            (void)process_handle;

            // Keep it conservative: return 0 for unknown types, but do write the output.
            u64 value = 0;
            // type 0 is commonly used for "process ID" or similar metadata in some runtimes;
            // return a stable non-zero to avoid divide-by-zero / sentinel checks.
            if (type == 0) value = 0x0000000000000008ULL;
            // Early Pokemon Sun runtime probes type 0x14 and expects a non-zero capability/count.
            if (type == 0x14u) value = 1;
            if (out_ptr != 0) cpu.memory_.write64(out_ptr, value);
            cpu.state().r[0] = 0;
            return true;
        }
        case 0x3C: {
            const auto& r = cpu.state().r;
            Logger::log("Kernel Break hit!  R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X R8=0x%08X R9=0x%08X R10=0x%08X R11=0x%08X R12=0x%08X SP=0x%08X LR=0x%08X PC=0x%08X\n",
                        r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], cpu.getPC());
            // On real hardware this is typically a non-returning debug/panic trap.
            // However, many retail titles will only reach it if our HLE stubs return
            // unexpected values (triggering asserts). Default to ignoring the break
            // so we can discover the next missing feature, but allow opting into a
            // hard stop for debugging.
            const char* stop = std::getenv("NEDOB_STOP_ON_BREAK");
            cpu.state().r[0] = 0;  // success
            return (stop && stop[0] == '1') ? false : true;
        }
        case 0x3D:
            Logger::log("SVC OutputDebugString (addr=0x%08X, len=%u)\n",
                        cpu.state().r[1], cpu.state().r[2]);
            cpu.state().r[0] = 0;
            return true;
        default:
            Logger::log("SVC 0x%02X %s (stub) at PC 0x%08X\n", svc_num,
                        getSvcName(svc_num), cpu.getPC());
            cpu.state().r[0] = 0;
            return true;
    }
}
