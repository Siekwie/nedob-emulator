#include "memory.hpp"
#include "../../common/logger.hpp"
#include <cstring>

namespace {
constexpr VAddr IO_STUB_VBLANK_OFFSET = 0x34;
constexpr u32 VBLANK_READY_MASK = 0x30000001u;
}

MemorySystem::MemorySystem() {
    fcram_.resize(FCRAM_SIZE, 0);
    process_memory_.resize(PROCESS_MEMORY_WITH_VRAM_SIZE, 0);
    vram_.resize(VRAM_SIZE, 0);
    system_info_mem_.resize(SYSTEM_INFO_SIZE, 0);
    shared_page_.resize(SHARED_PAGE_SIZE, 0);
    stack_mem_.resize(STACK_REGION_SIZE, 0);
    tls_mem_.resize(STACK_TLS_SIZE, 0);
    io_stub_.resize(IO_STUB_SIZE, 0);
    const u64 tick = 0x1000000ULL;
    shared_page_[0x08] = static_cast<u8>(tick);
    shared_page_[0x09] = static_cast<u8>(tick >> 8);
    shared_page_[0x0A] = static_cast<u8>(tick >> 16);
    shared_page_[0x0B] = static_cast<u8>(tick >> 24);
    shared_page_[0x0C] = static_cast<u8>(tick >> 32);
    shared_page_[0x0D] = static_cast<u8>(tick >> 40);
    shared_page_[0x0E] = static_cast<u8>(tick >> 48);
    shared_page_[0x0F] = static_cast<u8>(tick >> 56);
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
    (void)addr;
    (void)size;
    (void)out_offset;
    return false;
}

bool MemorySystem::tryProcessMemory(VAddr addr, std::size_t size, std::size_t& out_offset) const {
    if (addr >= STACK_TLS_VADDR && addr < STACK_TLS_VADDR_END) return false;
    if (addr >= STACK_REGION_VADDR && addr < STACK_REGION_VADDR_END) return false;
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
    if (unmapped_log_count_ < kMaxUnmappedLogPerFrame) {
        Logger::log("Memory: unmapped read8 addr=0x%08X -> 0\n", addr);
        ++unmapped_log_count_;
    }
    return 0;
}

u16 MemorySystem::read16(VAddr addr) {
    std::size_t offset = 0;
    std::vector<u8>* buf = nullptr;
    if (tryTls(addr, 2, offset)) buf = &tls_mem_;
    else if (tryStack(addr, 2, offset)) buf = &stack_mem_;
    else if (tryProcessMemory(addr, 2, offset)) buf = &process_memory_;
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
    if (unmapped_log_count_ < kMaxUnmappedLogPerFrame) {
        Logger::log("Memory: unmapped read16 addr=0x%08X -> 0\n", addr);
        ++unmapped_log_count_;
    }
    return 0;
}

u32 MemorySystem::read32(VAddr addr) {
    std::size_t offset = 0;
    std::vector<u8>* buf = nullptr;
    if (tryTls(addr, 4, offset)) buf = &tls_mem_;
    else if (tryStack(addr, 4, offset)) buf = &stack_mem_;
    else if (tryProcessMemory(addr, 4, offset)) buf = &process_memory_;
    else if (trySystemInfo(addr, 4, offset)) buf = &system_info_mem_;
    else if (trySharedPage(addr, 4, offset)) buf = &shared_page_;
    else if (tryFcram(addr, 4, offset)) buf = &fcram_;
    else if (tryVram(addr, 4, offset)) buf = (addr >= VRAM_VADDR && addr < VRAM_VADDR_END) ? &vram_ : &fcram_;
    if (buf) {
        return static_cast<u32>((*buf)[offset]) |
               (static_cast<u32>((*buf)[offset + 1]) << 8) |
               (static_cast<u32>((*buf)[offset + 2]) << 16) |
               (static_cast<u32>((*buf)[offset + 3]) << 24);
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
    if (unmapped_log_count_ < kMaxUnmappedLogPerFrame) {
        Logger::log("Memory: unmapped read32 addr=0x%08X -> 0\n", addr);
        ++unmapped_log_count_;
    }
    return 0;
}

u64 MemorySystem::read64(VAddr addr) {
    std::size_t offset = 0;
    std::vector<u8>* buf = nullptr;
    if (tryTls(addr, 8, offset)) buf = &tls_mem_;
    else if (tryStack(addr, 8, offset)) buf = &stack_mem_;
    else if (tryProcessMemory(addr, 8, offset)) buf = &process_memory_;
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
    std::size_t offset = 0;
    if (tryTls(addr, 1, offset)) { tls_mem_[offset] = value; return; }
    if (tryStack(addr, 1, offset)) { stack_mem_[offset] = value; return; }
    if (tryProcessMemory(addr, 1, offset)) { process_memory_[offset] = value; return; }
    if (trySystemInfo(addr, 1, offset)) { system_info_mem_[offset] = value; return; }
    if (trySharedPage(addr, 1, offset)) { shared_page_[offset] = value; return; }
    if (tryFcram(addr, 1, offset)) { fcram_[offset] = value; return; }
    if (tryVram(addr, 1, offset)) {
        (addr >= VRAM_VADDR && addr < VRAM_VADDR_END ? vram_ : fcram_)[offset] = value;
        return;
    }
    if (tryIoStub(addr, 1, offset)) { io_stub_[offset] = value; return; }
    if (unmapped_log_count_ < kMaxUnmappedLogPerFrame) {
        Logger::log("Memory: unmapped write8 addr=0x%08X value=0x%02X (no-op)\n", addr, value);
        ++unmapped_log_count_;
    }
}

void MemorySystem::write16(VAddr addr, u16 value) {
    std::size_t offset = 0;
    auto* buf = tryTls(addr, 2, offset) ? &tls_mem_
        : tryStack(addr, 2, offset) ? &stack_mem_
        : tryProcessMemory(addr, 2, offset) ? &process_memory_
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
        io_stub_[offset] = static_cast<u8>(value);
        io_stub_[offset + 1] = static_cast<u8>(value >> 8);
        return;
    }
    if (unmapped_log_count_ < kMaxUnmappedLogPerFrame) {
        Logger::log("Memory: unmapped write16 addr=0x%08X value=0x%04X (no-op)\n", addr, value);
        ++unmapped_log_count_;
    }
}

void MemorySystem::write32(VAddr addr, u32 value) {
    std::size_t offset = 0;
    auto* buf = tryTls(addr, 4, offset) ? &tls_mem_
        : tryStack(addr, 4, offset) ? &stack_mem_
        : tryProcessMemory(addr, 4, offset) ? &process_memory_
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
        io_stub_[offset] = static_cast<u8>(value);
        io_stub_[offset + 1] = static_cast<u8>(value >> 8);
        io_stub_[offset + 2] = static_cast<u8>(value >> 16);
        io_stub_[offset + 3] = static_cast<u8>(value >> 24);
        return;
    }
    if (unmapped_log_count_ < kMaxUnmappedLogPerFrame) {
        Logger::log("Memory: unmapped write32 addr=0x%08X value=0x%08X (no-op)\n", addr, value);
        ++unmapped_log_count_;
    }
}

void MemorySystem::write64(VAddr addr, u64 value) {
    write32(addr, static_cast<u32>(value));
    write32(addr + 4, static_cast<u32>(value >> 32));
}
