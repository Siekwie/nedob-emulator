#include "memory.hpp"
#include "../../common/logger.hpp"
#include <cstdlib>
#include <cctype>
#include <cstring>

namespace {
constexpr VAddr IO_STUB_VBLANK_OFFSET = 0x34;
constexpr u32 VBLANK_READY_MASK = 0x30000001u;
}

MemorySystem::MemorySystem() {
    fcram_.resize(FCRAM_SIZE, 0);
    process_memory_.resize(PROCESS_MEMORY_WITH_VRAM_SIZE, 0);
    // Shared-memory mapping extends beyond our flat process_memory_ region:
    // 0x10000000-0x11FFFFFF is in process_memory_, but 0x12000000-0x13FFFFFF is not.
    shared_memory_tail_.resize(static_cast<std::size_t>(SHARED_MEMORY_VADDR_END - PROCESS_MEMORY_VADDR_END), 0);
    vram_.resize(VRAM_SIZE, 0);
    system_info_mem_.resize(SYSTEM_INFO_SIZE, 0);
    shared_page_.resize(SHARED_PAGE_SIZE, 0);
    stack_mem_.resize(STACK_REGION_SIZE, 0);
    tls_mem_.resize(STACK_TLS_SIZE, 0);
    io_stub_.resize(IO_STUB_SIZE, 0);
    // Allow code to "call" into the IO stub region (used for bringup hacks / callbacks).
    // Place a tiny ARM stub at 0x04000000: `BX LR`.
    if (io_stub_.size() >= 4) {
        const u32 bx_lr = 0xE12FFF1Eu;
        io_stub_[0] = static_cast<u8>(bx_lr);
        io_stub_[1] = static_cast<u8>(bx_lr >> 8);
        io_stub_[2] = static_cast<u8>(bx_lr >> 16);
        io_stub_[3] = static_cast<u8>(bx_lr >> 24);
    }
    const u64 tick = 0x1000000ULL;
    shared_page_[0x08] = static_cast<u8>(tick);
    shared_page_[0x09] = static_cast<u8>(tick >> 8);
    shared_page_[0x0A] = static_cast<u8>(tick >> 16);
    shared_page_[0x0B] = static_cast<u8>(tick >> 24);
    shared_page_[0x0C] = static_cast<u8>(tick >> 32);
    shared_page_[0x0D] = static_cast<u8>(tick >> 40);
    shared_page_[0x0E] = static_cast<u8>(tick >> 48);
    shared_page_[0x0F] = static_cast<u8>(tick >> 56);

    // Minimal TLS bootstrap:
    // Some crt0/libc code spins on a word at TPIDRURO base (0x1FF82000).
    // Seed it with a non-zero pointer to our mapped TLS page.
    const u32 tls_word = STACK_TLS_VADDR;
    const std::size_t tls_off = static_cast<std::size_t>(TLS_AREA_VADDR - SYSTEM_INFO_VADDR);
    if (tls_off + 4 <= system_info_mem_.size()) {
        system_info_mem_[tls_off + 0] = static_cast<u8>(tls_word);
        system_info_mem_[tls_off + 1] = static_cast<u8>(tls_word >> 8);
        system_info_mem_[tls_off + 2] = static_cast<u8>(tls_word >> 16);
        system_info_mem_[tls_off + 3] = static_cast<u8>(tls_word >> 24);
    }

    // Seed the first TLS word to a non-zero value. Some startup code uses an atomic
    // init routine that spins while this is zero.
    if (tls_mem_.size() >= 4) {
        const u32 self = STACK_TLS_VADDR;
        tls_mem_[0] = static_cast<u8>(self);
        tls_mem_[1] = static_cast<u8>(self >> 8);
        tls_mem_[2] = static_cast<u8>(self >> 16);
        tls_mem_[3] = static_cast<u8>(self >> 24);
    }

    if (const char* v = std::getenv("NEDOB_BREAK_SPINS"); v && v[0] == '1') {
        break_spins_enabled_ = true;
    }
    if (const char* v = std::getenv("NEDOB_SPIN_THRESHOLD"); v && v[0] != '\0') {
        const unsigned long t = std::strtoul(v, nullptr, 0);
        if (t > 0 && t <= 1000000000ul) {
            spin_threshold_ = static_cast<u32>(t);
        }
    }
    if (const char* v = std::getenv("NEDOB_PATCH_PANIC_LOOP"); v && v[0] == '1') {
        patch_panic_loop_ = true;
    }
    if (const char* v = std::getenv("NEDOB_LOG_PANIC_STRING"); v && v[0] == '1') {
        log_panic_string_ = true;
    }
}

void MemorySystem::resetUnmappedLogCount() {
    unmapped_log_count_ = 0;
}

void MemorySystem::initProcessEnvironmentBlock(u64 program_id) {
    constexpr std::size_t PEB_SIZE = 256;
    constexpr std::size_t PROGRAM_ID_OFFSET = 0x1C;
    if (process_memory_.size() < PEB_SIZE) return;
    for (std::size_t i = 0; i < PEB_SIZE; ++i)
        process_memory_[i] = 0;
    for (std::size_t i = 0; i < 8; ++i)
        process_memory_[PROGRAM_ID_OFFSET + i] = static_cast<u8>(program_id >> (i * 8));
}

void MemorySystem::advanceSharedPageTick() {
    constexpr std::size_t kTickOffset = 0x08;
    if (shared_page_.size() < kTickOffset + 8) return;
    u64 tick = static_cast<u64>(shared_page_[kTickOffset]) |
               (static_cast<u64>(shared_page_[kTickOffset + 1]) << 8) |
               (static_cast<u64>(shared_page_[kTickOffset + 2]) << 16) |
               (static_cast<u64>(shared_page_[kTickOffset + 3]) << 24) |
               (static_cast<u64>(shared_page_[kTickOffset + 4]) << 32) |
               (static_cast<u64>(shared_page_[kTickOffset + 5]) << 40) |
               (static_cast<u64>(shared_page_[kTickOffset + 6]) << 48) |
               (static_cast<u64>(shared_page_[kTickOffset + 7]) << 56);
    if (tick == 0) tick = 1;
    tick += 0x10000ULL;
    shared_page_[kTickOffset]     = static_cast<u8>(tick);
    shared_page_[kTickOffset + 1] = static_cast<u8>(tick >> 8);
    shared_page_[kTickOffset + 2] = static_cast<u8>(tick >> 16);
    shared_page_[kTickOffset + 3] = static_cast<u8>(tick >> 24);
    shared_page_[kTickOffset + 4] = static_cast<u8>(tick >> 32);
    shared_page_[kTickOffset + 5] = static_cast<u8>(tick >> 40);
    shared_page_[kTickOffset + 6] = static_cast<u8>(tick >> 48);
    shared_page_[kTickOffset + 7] = static_cast<u8>(tick >> 56);
}

MemorySystem::~MemorySystem() = default;

void MemorySystem::mapCode(VAddr vaddr, const u8* data, std::size_t size) {
    if (!data || size == 0) return;
    std::size_t offset = 0;
    if (vaddr < PROCESS_MEMORY_VADDR_END && tryProcessMemory(vaddr, size, offset)) {
        if (offset + size <= process_memory_.size())
            std::memcpy(process_memory_.data() + offset, data, size);
    }
}

bool MemorySystem::tryFcram(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    // Some userland code (or stubs) may deal with physical FCRAM addresses directly.
    // Map 0x20000000..0x27FFFFFF to our backing FCRAM buffer.
    if (addr < FCRAM_PADDR || addr >= FCRAM_PADDR_END) return false;
    out_offset = static_cast<std::size_t>(addr - FCRAM_PADDR);
    return (out_offset + size) <= fcram_.size();
}

bool MemorySystem::tryProcessMemory(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr >= STACK_TLS_VADDR && addr < STACK_TLS_VADDR_END) return false;
    if (addr >= STACK_REGION_VADDR && addr < STACK_REGION_VADDR_END) return false;
    // This low address range is used by a bunch of legacy/HW-facing code paths.
    // Route it to our IO stub backing store instead of treating it as process RAM.
    if (addr >= IO_STUB_VADDR && addr < IO_STUB_VADDR_END) return false;
    if (addr < PROCESS_MEMORY_VADDR_END) {
        out_offset = addr;
        if (addr >= process_memory_.size()) return false;
        if (size > process_memory_.size() - addr) return false;
        return true;
    }
    if (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) {
        const std::size_t vram_offset = addr - VRAM_VADDR;
        out_offset = PROCESS_MEMORY_SIZE + vram_offset;
        if (out_offset + size > process_memory_.size()) return false;
        return true;  // VRAM 0x1F000000-0x1F5FFFFF backed in process_memory_ for boot buffers
    }
    return false;
}

bool MemorySystem::trySharedMemoryTail(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr < PROCESS_MEMORY_VADDR_END || addr >= SHARED_MEMORY_VADDR_END) return false;
    out_offset = addr - PROCESS_MEMORY_VADDR_END;
    return (out_offset + size) <= shared_memory_tail_.size();
}

bool MemorySystem::tryVram(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr >= VRAM_VADDR && addr + size <= VRAM_VADDR_END) {
        out_offset = addr - VRAM_VADDR;
        return true;
    }
    if (addr >= LINEAR_HEAP_VADDR && addr + size <= LINEAR_HEAP_VADDR_END) {
        out_offset = addr - LINEAR_HEAP_VADDR;
        return out_offset + size <= fcram_.size();
    }
    return false;
}

bool MemorySystem::trySystemInfo(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr < SYSTEM_INFO_VADDR || addr >= SYSTEM_INFO_VADDR_END) return false;
    out_offset = addr - SYSTEM_INFO_VADDR;
    return (out_offset + size) <= system_info_mem_.size();
}

bool MemorySystem::trySharedPage(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr < SHARED_PAGE_VADDR || addr >= SHARED_PAGE_VADDR_END) return false;
    out_offset = addr - SHARED_PAGE_VADDR;
    return (out_offset + size) <= shared_page_.size();
}

bool MemorySystem::tryTls(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr < STACK_TLS_VADDR || addr >= STACK_TLS_VADDR_END) return false;
    out_offset = addr - STACK_TLS_VADDR;
    return (out_offset + size) <= tls_mem_.size();
}

bool MemorySystem::tryStack(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr < STACK_REGION_VADDR || addr >= STACK_REGION_VADDR_END) return false;
    out_offset = addr - STACK_REGION_VADDR;
    return (out_offset + size) <= stack_mem_.size();
}

bool MemorySystem::tryIo(VAddr addr) const {
    return addr >= IO_AREA_VADDR && addr < IO_AREA_VADDR_END;
}

bool MemorySystem::tryIoStub(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr >= IO_STUB_VADDR && addr + size <= IO_STUB_VADDR_END) {
        out_offset = addr - IO_STUB_VADDR;
        return true;
    }
    return false;
}

u8* MemorySystem::getVramPointer(VAddr vaddr) {
    std::size_t off = 0;
    if (tryProcessMemory(vaddr, 1, off)) return process_memory_.data() + off;
    if (tryVram(vaddr, 1, off)) return fcram_.data() + off;
    return nullptr;
}

const u8* MemorySystem::getVramPointer(VAddr vaddr) const {
    std::size_t off = 0;
    if (tryProcessMemory(vaddr, 1, off)) return process_memory_.data() + off;
    if (tryVram(vaddr, 1, off)) return fcram_.data() + off;
    return nullptr;
}

VAddr MemorySystem::virtualToPhysical(VAddr vaddr) const {
    if (vaddr >= VRAM_VADDR && vaddr < VRAM_VADDR_END)
        return VRAM_PADDR + (vaddr - VRAM_VADDR);
    if (vaddr >= LINEAR_HEAP_VADDR && vaddr < LINEAR_HEAP_VADDR_END)
        return FCRAM_PADDR + (vaddr - LINEAR_HEAP_VADDR);
    if (vaddr >= PROCESS_IMAGE_VADDR && vaddr < PROCESS_IMAGE_VADDR_END)
        return FCRAM_PADDR + (vaddr - PROCESS_IMAGE_VADDR);
    if (vaddr >= HEAP_VADDR && vaddr < HEAP_VADDR_END)
        return FCRAM_PADDR + 0x04000000u + (vaddr - HEAP_VADDR);
    return vaddr;
}

u8 MemorySystem::read8(VAddr addr) {
    std::size_t offset = 0;
    if (tryTls(addr, 1, offset)) return tls_mem_[offset];
    if (tryStack(addr, 1, offset)) return stack_mem_[offset];
    if (tryProcessMemory(addr, 1, offset)) {
        return process_memory_[offset];
    }
    if (trySharedMemoryTail(addr, 1, offset)) return shared_memory_tail_[offset];
    if (trySystemInfo(addr, 1, offset)) return system_info_mem_[offset];
    if (trySharedPage(addr, 1, offset)) return shared_page_[offset];
    if (tryFcram(addr, 1, offset)) {
        return fcram_[offset];
    }
    if (tryVram(addr, 1, offset)) {
        return (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) ? vram_[offset] : fcram_[offset];
    }
    if (tryIoStub(addr, 1, offset)) {
        u8 v = io_stub_[offset];
        if (offset == IO_STUB_VBLANK_OFFSET) v |= 1u;
        return v;
    }
    if (tryIo(addr)) {
        return 0;
    }
    if (shouldLogUnmapped()) {
        if (has_current_access_pc_) {
            Logger::log("Memory: unmapped read8 pc=0x%08X addr=0x%08X -> 0\n", current_access_pc_, addr);
        } else {
            Logger::log("Memory: unmapped read8 addr=0x%08X -> 0\n", addr);
        }
    }
    return 0;
}

u16 MemorySystem::read16(VAddr addr) {
    std::size_t offset = 0;
    std::vector<u8>* buf = nullptr;
    if (tryTls(addr, 2, offset)) buf = &tls_mem_;
    else if (tryStack(addr, 2, offset)) buf = &stack_mem_;
    else if (tryProcessMemory(addr, 2, offset)) buf = &process_memory_;
    else if (trySharedMemoryTail(addr, 2, offset)) buf = &shared_memory_tail_;
    else if (trySystemInfo(addr, 2, offset)) buf = &system_info_mem_;
    else if (trySharedPage(addr, 2, offset)) buf = &shared_page_;
    else if (tryFcram(addr, 2, offset)) buf = &fcram_;
    else if (tryVram(addr, 2, offset)) buf = (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) ? &vram_ : &fcram_;
    if (buf) {
        return static_cast<u16>((*buf)[offset]) |
               (static_cast<u16>((*buf)[offset + 1]) << 8);
    }
    if (tryIoStub(addr, 2, offset)) {
        u16 v = static_cast<u16>(io_stub_[offset]) |
                (static_cast<u16>(io_stub_[offset + 1]) << 8);
        if (offset <= IO_STUB_VBLANK_OFFSET && offset + 2 > IO_STUB_VBLANK_OFFSET)
            v |= 1u;
        return v;
    }
    if (tryIo(addr)) return 0;
    if (shouldLogUnmapped()) {
        if (has_current_access_pc_) {
            Logger::log("Memory: unmapped read16 pc=0x%08X addr=0x%08X -> 0\n", current_access_pc_, addr);
        } else {
            Logger::log("Memory: unmapped read16 addr=0x%08X -> 0\n", addr);
        }
    }
    return 0;
}

u32 MemorySystem::read32(VAddr addr) {
    std::size_t offset = 0;
    std::vector<u8>* buf = nullptr;
    if (tryTls(addr, 4, offset)) buf = &tls_mem_;
    else if (tryStack(addr, 4, offset)) buf = &stack_mem_;
    else if (tryProcessMemory(addr, 4, offset)) buf = &process_memory_;
    else if (trySharedMemoryTail(addr, 4, offset)) buf = &shared_memory_tail_;
    else if (trySystemInfo(addr, 4, offset)) buf = &system_info_mem_;
    else if (trySharedPage(addr, 4, offset)) buf = &shared_page_;
    else if (tryFcram(addr, 4, offset)) buf = &fcram_;
    else if (tryVram(addr, 4, offset)) buf = (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) ? &vram_ : &fcram_;
    if (buf) {
        u32 v = static_cast<u32>((*buf)[offset]) |
                (static_cast<u32>((*buf)[offset + 1]) << 8) |
                (static_cast<u32>((*buf)[offset + 2]) << 16) |
                (static_cast<u32>((*buf)[offset + 3]) << 24);

        // Debug escape hatch for single-threaded bringup: if user code is spinning on a
        // process-memory word becoming non-zero, force it non-zero after enough identical reads.
        if (break_spins_enabled_ && has_current_access_pc_ && buf == &process_memory_ && v == 0) {
            if (spin_last_addr_ == addr && spin_last_pc_ == current_access_pc_) {
                ++spin_same_read_count_;
            } else {
                spin_last_addr_ = addr;
                spin_last_pc_ = current_access_pc_;
                spin_same_read_count_ = 1;
            }

            if (spin_same_read_count_ == spin_threshold_) {
                // Write a pointer to our IO-stub trampoline (0x04000000) directly to backing store.
                // This tends to satisfy "wait until callback ptr is non-null; then BLX it" codepaths.
                const u32 forced = IO_STUB_VADDR;
                (*buf)[offset + 0] = static_cast<u8>(forced);
                (*buf)[offset + 1] = static_cast<u8>(forced >> 8);
                (*buf)[offset + 2] = static_cast<u8>(forced >> 16);
                (*buf)[offset + 3] = static_cast<u8>(forced >> 24);
                v = forced;

                // If we hit the known panic/poll loop in Pokemon Sun bringup, try to extract the
                // message string and optionally patch the infinite loop into a return so we can
                // discover the next missing feature.
                if (current_access_pc_ == 0x001074C0u && addr == 0x0063DCCCu) {
                    if (log_panic_string_ && !panic_string_printed_) {
                        std::size_t msg_off = 0;
                        constexpr VAddr kMsgAddr = 0x001074E0u;
                        if (tryProcessMemory(kMsgAddr, 1, msg_off)) {
                            char msg[129];
                            std::size_t out_i = 0;
                            for (; out_i + 1 < sizeof(msg); ++out_i) {
                                const u8 ch = process_memory_[msg_off + out_i];
                                if (ch == 0) break;
                                msg[out_i] = (std::isprint(ch) || ch == '\n' || ch == '\t') ? static_cast<char>(ch) : '.';
                            }
                            msg[out_i] = '\0';
                            Logger::log("Panic loop message @0x%08X: \"%s\"\n", kMsgAddr, msg);
                            panic_string_printed_ = true;
                        }
                    }
                    if (patch_panic_loop_) {
                        // The loop body uses `BLX R2`, which overwrites LR with 0x001074D8.
                        // If we only patch the tail to `BX LR`, it will just jump to itself.
                        // Patch both sites:
                        // - replace `BLX R2` with `BX R2` (preserve caller LR)
                        // - replace the loop-back branch with `BX LR` (return to caller)
                        constexpr VAddr kPatchBlxAddr = 0x001074D4u;
                        constexpr VAddr kPatchTailAddr = 0x001074D8u;
                        std::size_t blx_off = 0, tail_off = 0;
                        const bool ok_blx = tryProcessMemory(kPatchBlxAddr, 4, blx_off);
                        const bool ok_tail = tryProcessMemory(kPatchTailAddr, 4, tail_off);
                        if (ok_blx && ok_tail) {
                            const u32 bx_r2 = 0xE12FFF12u;
                            const u32 bx_lr = 0xE12FFF1Eu;
                            process_memory_[blx_off + 0] = static_cast<u8>(bx_r2);
                            process_memory_[blx_off + 1] = static_cast<u8>(bx_r2 >> 8);
                            process_memory_[blx_off + 2] = static_cast<u8>(bx_r2 >> 16);
                            process_memory_[blx_off + 3] = static_cast<u8>(bx_r2 >> 24);
                            process_memory_[tail_off + 0] = static_cast<u8>(bx_lr);
                            process_memory_[tail_off + 1] = static_cast<u8>(bx_lr >> 8);
                            process_memory_[tail_off + 2] = static_cast<u8>(bx_lr >> 16);
                            process_memory_[tail_off + 3] = static_cast<u8>(bx_lr >> 24);
                            Logger::log("Panic loop patch: BX R2 @0x%08X, BX LR @0x%08X\n",
                                        kPatchBlxAddr, kPatchTailAddr);
                        }

                        // The caller at 0x00104868 uses `BL 0x001074BC`, which clobbers LR before
                        // entering the panic routine. If we want to "return" from the panic path,
                        // turn that BL into a plain B so LR stays as the caller's LR.
                        constexpr VAddr kPatchCallerBl = 0x00104868u;
                        std::size_t caller_off = 0;
                        if (tryProcessMemory(kPatchCallerBl, 4, caller_off)) {
                            const u32 w = static_cast<u32>(process_memory_[caller_off]) |
                                          (static_cast<u32>(process_memory_[caller_off + 1]) << 8) |
                                          (static_cast<u32>(process_memory_[caller_off + 2]) << 16) |
                                          (static_cast<u32>(process_memory_[caller_off + 3]) << 24);
                            if (w == 0xEB000B13u) {  // BL 0x001074BC
                                const u32 patched = (w & ~(1u << 24));  // B to same target
                                process_memory_[caller_off + 0] = static_cast<u8>(patched);
                                process_memory_[caller_off + 1] = static_cast<u8>(patched >> 8);
                                process_memory_[caller_off + 2] = static_cast<u8>(patched >> 16);
                                process_memory_[caller_off + 3] = static_cast<u8>(patched >> 24);
                                Logger::log("Panic loop patch: changed BL->B at 0x%08X\n", kPatchCallerBl);
                            }
                        }
                    }
                }
                if (!spin_notice_printed_) {
                    Logger::log("Memory: broke spin-wait at pc=0x%08X addr=0x%08X (forced 0x%08X)\n",
                                current_access_pc_, addr, forced);
                    spin_notice_printed_ = true;
                }
            }
        }

        return v;
    }
    if (tryIoStub(addr, 4, offset)) {
        u32 v = static_cast<u32>(io_stub_[offset]) |
                (static_cast<u32>(io_stub_[offset + 1]) << 8) |
                (static_cast<u32>(io_stub_[offset + 2]) << 16) |
                (static_cast<u32>(io_stub_[offset + 3]) << 24);
        if (offset == IO_STUB_VBLANK_OFFSET)
            v |= VBLANK_READY_MASK;
        return v;
    }
    if (tryIo(addr)) return 0;
    if (shouldLogUnmapped()) {
        if (has_current_access_pc_) {
            Logger::log("Memory: unmapped read32 pc=0x%08X addr=0x%08X -> 0\n", current_access_pc_, addr);
        } else {
            Logger::log("Memory: unmapped read32 addr=0x%08X -> 0\n", addr);
        }
    }
    return 0;
}

u64 MemorySystem::read64(VAddr addr) {
    std::size_t offset = 0;
    std::vector<u8>* buf = nullptr;
    if (tryTls(addr, 8, offset)) buf = &tls_mem_;
    else if (tryStack(addr, 8, offset)) buf = &stack_mem_;
    else if (tryProcessMemory(addr, 8, offset)) buf = &process_memory_;
    else if (trySharedMemoryTail(addr, 8, offset)) buf = &shared_memory_tail_;
    else if (trySystemInfo(addr, 8, offset)) buf = &system_info_mem_;
    else if (trySharedPage(addr, 8, offset)) buf = &shared_page_;
    else if (tryFcram(addr, 8, offset)) buf = &fcram_;
    else if (tryVram(addr, 8, offset)) buf = (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) ? &vram_ : &fcram_;
    if (buf) {
        return static_cast<u64>((*buf)[offset]) |
               (static_cast<u64>((*buf)[offset + 1]) << 8) |
               (static_cast<u64>((*buf)[offset + 2]) << 16) |
               (static_cast<u64>((*buf)[offset + 3]) << 24) |
               (static_cast<u64>((*buf)[offset + 4]) << 32) |
               (static_cast<u64>((*buf)[offset + 5]) << 40) |
               (static_cast<u64>((*buf)[offset + 6]) << 48) |
               (static_cast<u64>((*buf)[offset + 7]) << 56);
    }
    return (static_cast<u64>(read32(addr)) | (static_cast<u64>(read32(addr + 4)) << 32));
}

void MemorySystem::write8(VAddr addr, u8 value) {
    if (addr >= STACK_TLS_VADDR && addr < (STACK_TLS_VADDR + 4)) return;
    std::size_t offset = 0;
    if (tryTls(addr, 1, offset)) { tls_mem_[offset] = value; return; }
    if (tryStack(addr, 1, offset)) { stack_mem_[offset] = value; return; }
    if (tryProcessMemory(addr, 1, offset)) { process_memory_[offset] = value; return; }
    if (trySharedMemoryTail(addr, 1, offset)) { shared_memory_tail_[offset] = value; return; }
    if (trySystemInfo(addr, 1, offset)) { system_info_mem_[offset] = value; return; }
    if (trySharedPage(addr, 1, offset)) { shared_page_[offset] = value; return; }
    if (tryFcram(addr, 1, offset)) { fcram_[offset] = value; return; }
    if (tryVram(addr, 1, offset)) {
        (addr >= VRAM_VADDR && addr < VRAM_VADDR_END ? vram_ : fcram_)[offset] = value;
        return;
    }
    if (tryIoStub(addr, 1, offset)) {
        // Keep the trampoline at 0x04000000 intact (`BX LR`).
        if (offset < 4) return;
        io_stub_[offset] = value;
        return;
    }
    if (shouldLogUnmapped()) {
        if (has_current_access_pc_) {
            Logger::log("Memory: unmapped write8 pc=0x%08X addr=0x%08X value=0x%02X (no-op)\n",
                        current_access_pc_, addr, value);
        } else {
            Logger::log("Memory: unmapped write8 addr=0x%08X value=0x%02X (no-op)\n", addr, value);
        }
    }
}

void MemorySystem::write16(VAddr addr, u16 value) {
    if (addr >= STACK_TLS_VADDR && addr < (STACK_TLS_VADDR + 4)) return;
    std::size_t offset = 0;
    auto* buf = tryTls(addr, 2, offset) ? &tls_mem_
        : tryStack(addr, 2, offset) ? &stack_mem_
        : tryProcessMemory(addr, 2, offset) ? &process_memory_
        : trySharedMemoryTail(addr, 2, offset) ? &shared_memory_tail_
        : trySystemInfo(addr, 2, offset) ? &system_info_mem_
        : trySharedPage(addr, 2, offset) ? &shared_page_
        : tryFcram(addr, 2, offset) ? &fcram_
        : tryVram(addr, 2, offset) ? (addr >= VRAM_VADDR && addr < VRAM_VADDR_END ? &vram_ : &fcram_)
        : nullptr;
    if (buf) {
        (*buf)[offset] = static_cast<u8>(value);
        (*buf)[offset + 1] = static_cast<u8>(value >> 8);
        return;
    }
    if (tryIoStub(addr, 2, offset)) {
        if (offset < 4) return;
        io_stub_[offset] = static_cast<u8>(value);
        io_stub_[offset + 1] = static_cast<u8>(value >> 8);
        return;
    }
    if (shouldLogUnmapped()) {
        if (has_current_access_pc_) {
            Logger::log("Memory: unmapped write16 pc=0x%08X addr=0x%08X value=0x%04X (no-op)\n",
                        current_access_pc_, addr, value);
        } else {
            Logger::log("Memory: unmapped write16 addr=0x%08X value=0x%04X (no-op)\n", addr, value);
        }
    }
}

void MemorySystem::write32(VAddr addr, u32 value) {
    if (addr >= STACK_TLS_VADDR && addr < (STACK_TLS_VADDR + 4)) return;
    std::size_t offset = 0;
    auto* buf = tryTls(addr, 4, offset) ? &tls_mem_
        : tryStack(addr, 4, offset) ? &stack_mem_
        : tryProcessMemory(addr, 4, offset) ? &process_memory_
        : trySharedMemoryTail(addr, 4, offset) ? &shared_memory_tail_
        : trySystemInfo(addr, 4, offset) ? &system_info_mem_
        : trySharedPage(addr, 4, offset) ? &shared_page_
        : tryFcram(addr, 4, offset) ? &fcram_
        : tryVram(addr, 4, offset) ? (addr >= VRAM_VADDR && addr < VRAM_VADDR_END ? &vram_ : &fcram_)
        : nullptr;
    if (buf) {
        (*buf)[offset] = static_cast<u8>(value);
        (*buf)[offset + 1] = static_cast<u8>(value >> 8);
        (*buf)[offset + 2] = static_cast<u8>(value >> 16);
        (*buf)[offset + 3] = static_cast<u8>(value >> 24);
        return;
    }
    if (tryIoStub(addr, 4, offset)) {
        if (offset < 4) return;
        io_stub_[offset] = static_cast<u8>(value);
        io_stub_[offset + 1] = static_cast<u8>(value >> 8);
        io_stub_[offset + 2] = static_cast<u8>(value >> 16);
        io_stub_[offset + 3] = static_cast<u8>(value >> 24);
        return;
    }
    if (shouldLogUnmapped()) {
        if (has_current_access_pc_) {
            Logger::log("Memory: unmapped write32 pc=0x%08X addr=0x%08X value=0x%08X (no-op)\n",
                        current_access_pc_, addr, value);
        } else {
            Logger::log("Memory: unmapped write32 addr=0x%08X value=0x%08X (no-op)\n", addr, value);
        }
    }
}

void MemorySystem::write64(VAddr addr, u64 value) {
    write32(addr, static_cast<u32>(value));
    write32(addr + 4, static_cast<u32>(value >> 32));
}

bool MemorySystem::shouldLogUnmapped() {
    if (unmapped_total_logged_ >= kMaxUnmappedLogTotal || unmapped_log_count_ >= kMaxUnmappedLogPerFrame) {
        ++unmapped_total_suppressed_;
        logUnmappedSuppressedOnce();
        return false;
    }
    ++unmapped_log_count_;
    ++unmapped_total_logged_;
    return true;
}

void MemorySystem::logUnmappedSuppressedOnce() {
    if (unmapped_suppress_notice_printed_) return;
    if (unmapped_total_logged_ < kMaxUnmappedLogTotal) return;
    unmapped_suppress_notice_printed_ = true;
    Logger::log("Memory: unmapped access logging suppressed after %llu entries (further unmapped accesses won't be logged)\n",
                static_cast<unsigned long long>(unmapped_total_logged_));
}
