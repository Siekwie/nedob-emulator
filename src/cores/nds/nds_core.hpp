#pragma once

#include "../../common/emulator_core.hpp"
#include "memory.hpp"
#include "arm_cpu.hpp"
#include <string>
#include <memory>
#include <vector>

// Forward declarations
class Frontend;

/**
 * Nintendo DS emulator core.
 * 
 * Implements the NDS hardware:
 * - ARM9 and ARM7 CPU cores
 * - Memory management unit (MMU)
 * - Graphics (PPU A/B, 3D engine)
 * - Audio (SPU)
 * - Input and touchscreen
 * - Cartridge interface
 */
class NDSCore : public EmulatorCore {
public:
    NDSCore();
    ~NDSCore() override;
    
    // EmulatorCore interface
    bool loadROM(const std::string& rom_path) override;
    void reset() override;
    void runFrame() override;
    void handleEvent(const SDL_Event& event) override;
    void setFrontend(Frontend* frontend) override;
    bool saveState(const std::string& filepath) override;
    bool loadState(const std::string& filepath) override;
    const char* getName() const override { return "NDS"; }
    
    /**
     * Get PPU for rendering.
     */
    class PPU* getPPU() const { return ppu_.get(); }

private:
    Frontend* frontend_;
    
    // ROM data
    std::vector<uint8_t> rom_data_;
    std::string rom_path_;
    bool rom_loaded_;
    
    // Hardware components
    std::unique_ptr<NDSMemory> memory_;
    std::unique_ptr<ARMCpu> arm9_;
    std::unique_ptr<ARMCpu> arm7_;
    std::unique_ptr<class BIOSHLE> bios_hle_;
    std::unique_ptr<class PPU> ppu_;
    std::unique_ptr<class InterruptController> arm9_interrupts_;
    std::unique_ptr<class InterruptController> arm7_interrupts_;
    std::unique_ptr<class TimerSystem> arm9_timers_;
    std::unique_ptr<class TimerSystem> arm7_timers_;
    std::unique_ptr<class IPC> ipc_;
    std::unique_ptr<class DMA> dma_;
    
    // Frame timing
    uint32_t cycles_per_frame_;
    uint32_t cycles_this_frame_;
    
    /**
     * Initialize the core's hardware components.
     */
    void initializeHardware();
    
    /**
     * Load ROM file from disk.
     */
    bool loadROMFile(const std::string& path);
    
    /**
     * Parse NDS ROM header and initialize memory map.
     */
    bool parseROMHeader();
    
    /**
     * Run CPUs for one frame (approximately 33.5M cycles at 67MHz).
     */
    void runFrameCycles();
};
