#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

/**
 * NDS Memory Management Unit (MMU)
 * 
 * Handles all memory access for the NDS system.
 * The NDS has separate memory regions for ARM9 and ARM7,
 * with some shared regions.
 */
class NDSMemory {
public:
    NDSMemory();
    ~NDSMemory();
    
    /**
     * Initialize memory regions.
     */
    void initialize();
    
    /**
     * Load ROM data into memory.
     * 
     * @param rom_data ROM file data
     * @param arm9_offset Offset of ARM9 binary in ROM
     * @param arm9_entry ARM9 entry address
     * @param arm9_ram_addr ARM9 RAM address
     * @param arm9_size ARM9 binary size
     * @param arm7_offset Offset of ARM7 binary in ROM
     * @param arm7_entry ARM7 entry address
     * @param arm7_ram_addr ARM7 RAM address
     * @param arm7_size ARM7 binary size
     */
    bool loadROM(const std::vector<uint8_t>& rom_data,
                 uint32_t arm9_offset, uint32_t arm9_entry, uint32_t arm9_ram_addr, uint32_t arm9_size,
                 uint32_t arm7_offset, uint32_t arm7_entry, uint32_t arm7_ram_addr, uint32_t arm7_size);
    
    /**
     * Read 8-bit value from ARM9 address space.
     */
    uint8_t read8_ARM9(uint32_t address);
    
    /**
     * Read 16-bit value from ARM9 address space (aligned).
     */
    uint16_t read16_ARM9(uint32_t address);
    
    /**
     * Read 32-bit value from ARM9 address space (aligned).
     */
    uint32_t read32_ARM9(uint32_t address);
    
    /**
     * Write 8-bit value to ARM9 address space.
     */
    void write8_ARM9(uint32_t address, uint8_t value);
    
    /**
     * Write 16-bit value to ARM9 address space (aligned).
     */
    void write16_ARM9(uint32_t address, uint16_t value);
    
    /**
     * Write 32-bit value to ARM9 address space (aligned).
     */
    void write32_ARM9(uint32_t address, uint32_t value);
    
    /**
     * Read 8-bit value from ARM7 address space.
     */
    uint8_t read8_ARM7(uint32_t address);
    
    /**
     * Read 16-bit value from ARM7 address space (aligned).
     */
    uint16_t read16_ARM7(uint32_t address);
    
    /**
     * Read 32-bit value from ARM7 address space (aligned).
     */
    uint32_t read32_ARM7(uint32_t address);
    
    /**
     * Write 8-bit value to ARM7 address space.
     */
    void write8_ARM7(uint32_t address, uint8_t value);
    
    /**
     * Write 16-bit value to ARM7 address space (aligned).
     */
    void write16_ARM7(uint32_t address, uint16_t value);
    
    /**
     * Write 32-bit value to ARM7 address space (aligned).
     */
    void write32_ARM7(uint32_t address, uint32_t value);
    
    /**
     * Get pointer to memory region (for direct access when safe).
     * Returns nullptr if address is not in a valid region.
     */
    uint8_t* getPointer(uint32_t address, bool arm9);
    
    /**
     * Set interrupt controller for I/O register access.
     */
    void setInterruptController(class InterruptController* interrupts, bool is_arm9);
    
    /**
     * Set timer system for I/O register access.
     */
    void setTimerSystem(class TimerSystem* timers, bool is_arm9);

    /**
     * Set IPC system for FIFO communication.
     */
    void setIPC(class IPC* ipc);

    /**
     * Set DMA controller for DMA register access.
     */
    void setDMA(class DMA* dma);

    /**
     * Set PPU for VCOUNT/DISPSTAT register access.
     */
    void setPPU(class PPU* ppu);

    /**
     * Set ITCM base address (called from CP15).
     */
    void setITCMBase(uint32_t base);

    /**
     * Set DTCM base address (called from CP15).
     */
    void setDTCMBase(uint32_t base);
    
    /**
     * Set ITCM enabled state (called from CP15 C1).
     */
    void setITCMEnabled(bool enabled);
    
    /**
     * Set DTCM enabled state (called from CP15 C1).
     */
    void setDTCMEnabled(bool enabled);
    
    /**
     * Update ROM/Game Card state (called from runFrameCycles).
     * Decrements busy cycles and clears busy flag when ready.
     */
    void updateROMState(uint32_t cycles);
    
    /**
     * Read I/O register directly (for PPU to avoid circular dependency).
     * This bypasses the read handler to prevent infinite recursion.
     */
    uint16_t readIORegisterDirect(uint32_t address) const;

private:
    // ARM9 Memory Regions
    std::vector<uint8_t> arm9_bios_;      // 16KB ARM9 BIOS (will be HLE'd)
    std::vector<uint8_t> arm9_main_ram_;  // 4MB Main RAM
    std::vector<uint8_t> arm9_shared_wram_; // 32KB Shared WRAM (ARM9 view)
    std::vector<uint8_t> arm9_io_registers_; // I/O registers
    std::vector<uint8_t> arm9_palette_;   // 1KB Palette RAM
    std::vector<uint8_t> arm9_vram_;      // 656KB VRAM
    std::vector<uint8_t> arm9_itcm_;      // 32KB ITCM (Instruction TCM)
    std::vector<uint8_t> arm9_dtcm_;      // 16KB DTCM (Data TCM)
    
    // TCM base addresses (set via CP15)
    uint32_t itcm_base_;  // ITCM base address (default 0x00000000)
    uint32_t dtcm_base_;  // DTCM base address (default 0x0B000000)
    bool itcm_enabled_;   // ITCM enabled flag
    bool dtcm_enabled_;   // DTCM enabled flag
    
    // ARM7 Memory Regions
    std::vector<uint8_t> arm7_bios_;      // 16KB ARM7 BIOS (will be HLE'd)
    std::vector<uint8_t> arm7_main_ram_;  // 4MB Main RAM (shared with ARM9)
    std::vector<uint8_t> arm7_shared_wram_; // 32KB Shared WRAM (ARM7 view)
    std::vector<uint8_t> arm7_io_registers_; // I/O registers
    std::vector<uint8_t> arm7_wram_;      // 64KB ARM7 WRAM
    
    // ROM data (stored for cartridge access)
    std::vector<uint8_t> rom_data_;
    
    // ROM/Game Card state
    uint32_t rom_read_offset_;      // Current ROM read offset
    uint32_t rom_busy_cycles_;      // Cycles remaining until ROM ready
    bool rom_busy_;                 // ROM busy flag (bit 31 of ROMCTRL)
    
    // Sparse memory for unmapped regions that games write to
    // Key: page-aligned address (4KB pages), Value: page data
    std::unordered_map<uint32_t, std::vector<uint8_t>> sparse_memory_arm9_;
    std::unordered_map<uint32_t, std::vector<uint8_t>> sparse_memory_arm7_;
    static constexpr uint32_t SPARSE_PAGE_SIZE = 0x1000;  // 4KB pages
    
    /**
     * Map ARM9 address to memory region.
     * Returns nullptr for unmapped read-only regions (which should return 0).
     */
    uint8_t* mapAddress_ARM9(uint32_t address);
    
    /**
     * Map ARM7 address to memory region.
     * Returns nullptr for unmapped read-only regions (which should return 0).
     */
    uint8_t* mapAddress_ARM7(uint32_t address);
    
    /**
     * Get or create a sparse memory page for writes to unmapped regions.
     */
    uint8_t* getSparsePage(uint32_t address, bool is_arm9);
    
    // Interrupt controllers, timers, IPC, DMA, and PPU (for I/O register access)
    class InterruptController* arm9_interrupts_;
    class InterruptController* arm7_interrupts_;
    class TimerSystem* arm9_timers_;
    class TimerSystem* arm7_timers_;
    class IPC* ipc_;
    class DMA* dma_;
    class PPU* ppu_;
    
    /**
     * Check if address is in range.
     */
    bool isValidAddress(uint32_t address, uint32_t base, uint32_t size);
};
