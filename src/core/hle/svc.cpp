#include "svc.hpp"
#include "../arm/interpreter.hpp"
#include "../3ds/memory.hpp"
#include "../../common/logger.hpp"
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
    Logger::log("SVC 0x%02X %s  R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X  PC=0x%08X\n",
                svc_num, getSvcName(svc_num), r[0], r[1], r[2], r[3], cpu.getPC());

    if (svc_num == 0x2D && svc_num < kMaxSvc && handlers_[0x2D]) {
        const u32 out_ptr = cpu.state().r[0];
        const u32 name_ptr = cpu.state().r[1];
        const bool cont = handlers_[0x2D](svc_num, cpu);
        if (cont && cpu.state().r[0] != 0) {
            std::string name = readStringFromMemory(cpu.memory_, name_ptr);
            Logger::log("ConnectToPort: %s\n", name.c_str());
            u32 h = allocHandle(name);
            cpu.memory_.write32(out_ptr, h);
            cpu.state().r[0] = 0;
        }
        return cont;
    }

    if (svc_num < kMaxSvc && handlers_[svc_num]) {
        return handlers_[svc_num](svc_num, cpu);
    }

    switch (svc_num) {
        case 0x01:
            Logger::log("ControlMemory: addr=0x%08X size=0x%08X op=0x%08X (R3=0x%08X)\n",
                        cpu.state().r[0], cpu.state().r[1], cpu.state().r[2], cpu.state().r[3]);
            cpu.state().r[0] = 0;
            return true;
        case 0x38: {
            // GetResourceLimit: return kernel pseudo-handle 0xFFFF8001 (process resource limit). Do not use handle table.
            constexpr u32 RESOURCE_LIMIT_PSEUDO_HANDLE = 0xFFFF8001u;
            cpu.state().r[0] = 0;
            cpu.state().r[1] = RESOURCE_LIMIT_PSEUDO_HANDLE;
            return true;
        }
        case 0x23: {
            // CloseHandle: pseudo-handles (> 0xFFFF0000) are no-ops; do not modify handle table.
            const u32 handle = cpu.state().r[0];
            if (handle != 0 && handle <= 0xFFFF0000u) handle_table_.erase(handle);
            cpu.state().r[0] = 0;
            return true;
        }
        case 0x22:
            cpu.state().r[0] = 0;
            cpu.state().r[1] = 0x00000008u;  // GetProcessId: process ID in R1 (3DS ABI), must be non-zero
            return true;
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
        case 0x3A: {
            // GetResourceLimitLimitValues/CurrentValues: R0 = handle, R1 = handle (do not use as ptr!). Resource IDs often in R0 or separate buffer.
            // Use R0 only if it looks like a valid pointer (< 0x80000000); else assume one entry with ID 0 to avoid reading handle as address.
            const u32 r0 = cpu.state().r[0];
            const u32 r1_handle = cpu.state().r[1];
            const u32 out_addr = cpu.state().r[2];
            u32 count = cpu.state().r[3];
            if (count > 16u) count = 16u;
            const bool r0_looks_like_ptr = (r0 != 0 && r0 < 0x80000000u);
            const u32 ids_ptr = r0_looks_like_ptr ? r0 : 0;
            constexpr u64 COMMIT_LIMIT = 0x08000000ULL;
            constexpr u64 COMMIT_CURRENT = 0x02000000ULL;
            constexpr u64 PRIORITY_LIMIT = 64ULL;
            constexpr u64 PRIORITY_CURRENT = 1ULL;
            (void)r1_handle;
            for (u32 i = 0; i < count; ++i) {
                u8 rid = 0;
                if (ids_ptr != 0) rid = static_cast<u8>(cpu.memory_.read8(ids_ptr + i));
                u64 limit = 0, current = 0;
                if (rid == 0) { limit = COMMIT_LIMIT;   current = COMMIT_CURRENT; }
                else if (rid == 1) { limit = PRIORITY_LIMIT; current = PRIORITY_CURRENT; }
                cpu.memory_.write64(out_addr + i * 16, limit);
                cpu.memory_.write64(out_addr + i * 16 + 8, current);
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
            u32 id = allocHandle("Event");
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
        case 0x3C: {
            const auto& r = cpu.state().r;
            Logger::log("Kernel Break hit!  R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X R4=0x%08X R5=0x%08X R6=0x%08X R7=0x%08X R8=0x%08X R9=0x%08X R10=0x%08X R11=0x%08X R12=0x%08X SP=0x%08X LR=0x%08X PC=0x%08X\n",
                        r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], cpu.getPC());
            return true;
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
