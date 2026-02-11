#pragma once

#include "../../common/common_types.hpp"
#include <cstddef>
#include <vector>

/// Physical memory regions as seen from the ARM11
enum : PAddr {
    /// IO register area
    IO_AREA_PADDR = 0x10100000,
    IO_AREA_SIZE = 0x00400000, ///< IO area size (4MB)
    IO_AREA_PADDR_END = IO_AREA_PADDR + IO_AREA_SIZE,

    /// MPCore internal memory region
    MPCORE_RAM_PADDR = 0x17E00000,
    MPCORE_RAM_SIZE = 0x00002000, ///< MPCore internal memory size (8KB)
    MPCORE_RAM_PADDR_END = MPCORE_RAM_PADDR + MPCORE_RAM_SIZE,

    /// Video memory
    VRAM_PADDR = 0x18000000,
    VRAM_SIZE = 0x00600000, ///< VRAM size (6MB)
    VRAM_PADDR_END = VRAM_PADDR + VRAM_SIZE,

    /// New 3DS additional memory. Supposedly faster than regular FCRAM. Part of it can be used by
    /// applications and system modules if mapped via the ExHeader.
    N3DS_EXTRA_RAM_PADDR = 0x1F000000,
    N3DS_EXTRA_RAM_SIZE = 0x00400000, ///< New 3DS additional memory size (4MB)
    N3DS_EXTRA_RAM_PADDR_END = N3DS_EXTRA_RAM_PADDR + N3DS_EXTRA_RAM_SIZE,

    /// DSP memory
    DSP_RAM_PADDR = 0x1FF00000,
    DSP_RAM_SIZE = 0x00080000, ///< DSP memory size (512KB)
    DSP_RAM_PADDR_END = DSP_RAM_PADDR + DSP_RAM_SIZE,

    /// AXI WRAM
    AXI_WRAM_PADDR = 0x1FF80000,
    AXI_WRAM_SIZE = 0x00080000, ///< AXI WRAM size (512KB)
    AXI_WRAM_PADDR_END = AXI_WRAM_PADDR + AXI_WRAM_SIZE,

    /// Main FCRAM
    FCRAM_PADDR = 0x20000000,
    FCRAM_SIZE = 0x08000000,      ///< FCRAM size on the Old 3DS (128MB)
    FCRAM_N3DS_SIZE = 0x10000000, ///< FCRAM size on the New 3DS (256MB)
    FCRAM_PADDR_END = FCRAM_PADDR + FCRAM_SIZE,
    FCRAM_N3DS_PADDR_END = FCRAM_PADDR + FCRAM_N3DS_SIZE,
};

/// Virtual user-space memory regions
enum : VAddr {
    /// Where the application text, data and bss reside.
    PROCESS_IMAGE_VADDR = 0x00100000,
    PROCESS_IMAGE_MAX_SIZE = 0x03F00000,
    PROCESS_IMAGE_VADDR_END = PROCESS_IMAGE_VADDR + PROCESS_IMAGE_MAX_SIZE,

    /// Area where IPC buffers are mapped onto.
    IPC_MAPPING_VADDR = 0x04000000,
    IPC_MAPPING_SIZE = 0x04000000,
    IPC_MAPPING_VADDR_END = IPC_MAPPING_VADDR + IPC_MAPPING_SIZE,

    /// Application heap (includes stack). Backed by process_memory_ (not FCRAM).
    HEAP_VADDR = 0x08000000,
    HEAP_SIZE = 0x08000000,
    HEAP_VADDR_END = HEAP_VADDR + HEAP_SIZE,
    /// Process memory: every address from 0x00000000 to 0x11FFFFFF is valid (offset = addr). VRAM at offset PROCESS_MEMORY_SIZE.
    PROCESS_MEMORY_VADDR_END = 0x12000000,
    PROCESS_MEMORY_SIZE = 0x12000000,
    PROCESS_MEMORY_WITH_VRAM_SIZE = 0x12000000 + VRAM_SIZE,

    /// Dedicated stack region (isolated from code and TLS). SP at 0x0FB00000; large gap to TLS.
    STACK_REGION_VADDR = 0x0F800000,
    STACK_REGION_SIZE = 0x007FF000,   // ~8 MB up to TLS
    STACK_REGION_VADDR_END = 0x0FFFF000,

    /// Stack/TLS region at bottom of user heap: 0x0FFFF000-0x0FFFFFFF (4 KB), separate from process_memory_.
    STACK_TLS_VADDR = 0x0FFFF000,
    STACK_TLS_SIZE = 0x00001000,
    STACK_TLS_VADDR_END = 0x10000000,

    /// Area where shared memory buffers are mapped onto.
    SHARED_MEMORY_VADDR = 0x10000000,
    SHARED_MEMORY_SIZE = 0x04000000,
    SHARED_MEMORY_VADDR_END = SHARED_MEMORY_VADDR + SHARED_MEMORY_SIZE,

    /// Maps 1:1 to an offset in FCRAM. Used for HW allocations that need to be linear in physical
    /// memory.
    LINEAR_HEAP_VADDR = 0x14000000,
    LINEAR_HEAP_SIZE = 0x08000000,
    LINEAR_HEAP_VADDR_END = LINEAR_HEAP_VADDR + LINEAR_HEAP_SIZE,

    /// Maps 1:1 to New 3DS additional memory
    N3DS_EXTRA_RAM_VADDR = 0x1E800000,
    N3DS_EXTRA_RAM_VADDR_END = N3DS_EXTRA_RAM_VADDR + N3DS_EXTRA_RAM_SIZE,

    /// Maps 1:1 to the IO register area.
    IO_AREA_VADDR = 0x1EC00000,
    IO_AREA_VADDR_END = IO_AREA_VADDR + IO_AREA_SIZE,

    /// Stubbed IO region for game access at 0x04000000 (read returns stored value; used for GSP/GPU polling).
    IO_STUB_VADDR = 0x04000000,
    IO_STUB_SIZE = 0x00010000,
    IO_STUB_VADDR_END = IO_STUB_VADDR + IO_STUB_SIZE,

    /// Maps 1:1 to VRAM.
    VRAM_VADDR = 0x1F000000,
    VRAM_VADDR_END = VRAM_VADDR + VRAM_SIZE,
    /// VRAM mirror used by GPU framebuffer (e.g. fb_addr=0x01F0A000).
    VRAM_MIRROR_VADDR = 0x01F00000,
    VRAM_MIRROR_VADDR_END = VRAM_MIRROR_VADDR + VRAM_SIZE,

    /// Maps 1:1 to DSP memory.
    DSP_RAM_VADDR = 0x1FF00000,
    DSP_RAM_VADDR_END = DSP_RAM_VADDR + DSP_RAM_SIZE,

    /// KRT0 / kernel static config: 64 KB at 0x1FF80000 (system info, kernel config).
    SYSTEM_INFO_VADDR = 0x1FF80000,
    SYSTEM_INFO_SIZE = 0x00010000,
    SYSTEM_INFO_VADDR_END = SYSTEM_INFO_VADDR + SYSTEM_INFO_SIZE,

    /// Read-only page containing kernel and system configuration values (legacy alias).
    CONFIG_MEMORY_VADDR = 0x1FF80000,
    CONFIG_MEMORY_SIZE = 0x00001000,
    CONFIG_MEMORY_VADDR_END = CONFIG_MEMORY_VADDR + CONFIG_MEMORY_SIZE,

    /// Shared page: system time, settings; games read from here.
    SHARED_PAGE_VADDR = 0x1FF81000,
    SHARED_PAGE_SIZE = 0x00001000,
    SHARED_PAGE_VADDR_END = SHARED_PAGE_VADDR + SHARED_PAGE_SIZE,

    /// Area where TLS (Thread-Local Storage) buffers are allocated.
    TLS_AREA_VADDR = 0x1FF82000,
    TLS_ENTRY_SIZE = 0x200,

    /// Equivalent to LINEAR_HEAP_VADDR, but expanded to cover the extra memory in the New 3DS.
    NEW_LINEAR_HEAP_VADDR = 0x30000000,
    NEW_LINEAR_HEAP_SIZE = 0x10000000,
    NEW_LINEAR_HEAP_VADDR_END = NEW_LINEAR_HEAP_VADDR + NEW_LINEAR_HEAP_SIZE,

    /// Area where 3GX plugin framebuffers are stored
    PLUGIN_3GX_FB_VADDR = 0x06000000,
    PLUGIN_3GX_FB_SIZE = 0x000A9000,
    PLUGIN_3GX_FB_VADDR_END = PLUGIN_3GX_FB_VADDR + PLUGIN_3GX_FB_SIZE
};

/// Simple memory system with flat mapping. Process image and heap map to FCRAM.
/// VRAM is backed for GPU framebuffers. IO area returns 0 on read, ignores writes.
class MemorySystem {
public:
    MemorySystem();
    ~MemorySystem();

    MemorySystem(const MemorySystem&) = delete;
    MemorySystem& operator=(const MemorySystem&) = delete;

    /// Map code/data at virtual address. Overwrites any existing mapping.
    void mapCode(VAddr vaddr, const u8* data, std::size_t size);

    /// Reset unmapped-access log counter (call once per frame to limit log spam).
    void resetUnmappedLogCount();

    /// Advance the 64-bit tick in the shared page (0x1FF81000+0x08) by a small delta. Call once per frame.
    void advanceSharedPageTick();

    /// Fill first 256 bytes of process memory (PEB at 0x00000000) with boot values; program_id at 0x1C (LE 8 bytes).
    void initProcessEnvironmentBlock(u64 program_id);

    u8 read8(VAddr addr);
    u16 read16(VAddr addr);
    u32 read32(VAddr addr);
    u64 read64(VAddr addr);
    void write8(VAddr addr, u8 value);
    void write16(VAddr addr, u16 value);
    void write32(VAddr addr, u32 value);
    void write64(VAddr addr, u64 value);

    /// Get writable pointer for physical region (for GPU/framebuffer access)
    u8* getVramPointer(VAddr vaddr);
    const u8* getVramPointer(VAddr vaddr) const;

    /// Convert virtual to physical address for GPU. Returns vaddr if no translation.
    VAddr virtualToPhysical(VAddr vaddr) const;

private:
    bool tryFcram(VAddr addr, std::size_t size, std::size_t& out_offset) const;
    bool tryProcessMemory(VAddr addr, std::size_t size, std::size_t& out_offset) const;
    bool tryVram(VAddr addr, std::size_t size, std::size_t& out_offset) const;
    bool trySystemInfo(VAddr addr, std::size_t size, std::size_t& out_offset) const;
    bool trySharedPage(VAddr addr, std::size_t size, std::size_t& out_offset) const;
    bool tryTls(VAddr addr, std::size_t size, std::size_t& out_offset) const;
    bool tryStack(VAddr addr, std::size_t size, std::size_t& out_offset) const;
    bool tryIo(VAddr addr) const;
    bool tryIoStub(VAddr addr, std::size_t size, std::size_t& out_offset) const;

    std::vector<u8> fcram_;
    std::vector<u8> process_memory_;
    std::vector<u8> vram_;
    std::vector<u8> system_info_mem_;
    std::vector<u8> shared_page_;
    std::vector<u8> stack_mem_;
    std::vector<u8> tls_mem_;
    std::vector<u8> io_stub_;
    unsigned unmapped_log_count_{0};
    static constexpr unsigned kMaxUnmappedLogPerFrame = 50;
};