#include "nds_core.hpp"
#include "memory.hpp"
#include "arm_cpu.hpp"
#include "bios_hle.hpp"
#include "ppu.hpp"
#include "interrupts.hpp"
#include "timers.hpp"
#include "ipc.hpp"
#include "dma.hpp"
#include "../../frontend/frontend.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <cstring>

// NDS ROM header structure (simplified)
struct NDSHeader {
    char game_title[12];
    char game_code[4];
    char maker_code[2];
    uint8_t unit_code;
    uint8_t encryption_seed;
    uint8_t reserved[7];
    uint8_t game_version;
    uint8_t auto_start;
    uint32_t arm9_rom_offset;
    uint32_t arm9_entry_address;
    uint32_t arm9_ram_address;
    uint32_t arm9_size;
    uint32_t arm7_rom_offset;
    uint32_t arm7_entry_address;
    uint32_t arm7_ram_address;
    uint32_t arm7_size;
    // ... more fields, but we'll start simple
};

NDSCore::NDSCore()
    : frontend_(nullptr)
    , rom_loaded_(false)
    , cycles_per_frame_(33554432)  // ~33.5M cycles per frame (67MHz * 0.5s)
    , cycles_this_frame_(0)
{
    initializeHardware();
}

NDSCore::~NDSCore() {
    // Cleanup handled by member destructors
}

void NDSCore::initializeHardware() {
    // Initialize memory
    memory_ = std::make_unique<NDSMemory>();
    memory_->initialize();
    
    // Initialize interrupt controllers
    arm9_interrupts_ = std::make_unique<InterruptController>();
    arm7_interrupts_ = std::make_unique<InterruptController>();
    
    // Initialize timers
    arm9_timers_ = std::make_unique<TimerSystem>(arm9_interrupts_.get(), true);
    arm7_timers_ = std::make_unique<TimerSystem>(arm7_interrupts_.get(), false);

    // Initialize IPC
    ipc_ = std::make_unique<IPC>();
    ipc_->setInterruptController(arm9_interrupts_.get(), true);
    ipc_->setInterruptController(arm7_interrupts_.get(), false);
    
    // Initialize DMA
    dma_ = std::make_unique<DMA>();
    dma_->setMemory(memory_.get());
    dma_->setInterruptController(arm9_interrupts_.get(), true);
    dma_->setInterruptController(arm7_interrupts_.get(), false);
    
    // Initialize BIOS HLE
    bios_hle_ = std::make_unique<BIOSHLE>(memory_.get());
    
    // Initialize CPUs
    arm9_ = std::make_unique<ARMCpu>(memory_.get(), true);
    arm7_ = std::make_unique<ARMCpu>(memory_.get(), false);
    
    // Connect BIOS HLE and interrupts to CPUs
    arm9_->setBIOSHLE(bios_hle_.get());
    arm7_->setBIOSHLE(bios_hle_.get());
    arm9_->setInterruptController(arm9_interrupts_.get());
    arm7_->setInterruptController(arm7_interrupts_.get());
    arm9_->setMemory(memory_.get());  // Allow ARM9 to configure TCM via CP15
    
    // Initialize PPU
    ppu_ = std::make_unique<PPU>(memory_.get());
    ppu_->initialize();
    ppu_->setInterruptController(arm9_interrupts_.get(), true);
    ppu_->setInterruptController(arm7_interrupts_.get(), false);
    
    // Connect interrupt controllers, timers, IPC, and DMA to memory for I/O register access
    memory_->setInterruptController(arm9_interrupts_.get(), true);
    memory_->setInterruptController(arm7_interrupts_.get(), false);
    memory_->setTimerSystem(arm9_timers_.get(), true);
    memory_->setTimerSystem(arm7_timers_.get(), false);
    memory_->setIPC(ipc_.get());
    memory_->setDMA(dma_.get());
    memory_->setPPU(ppu_.get());
    
    std::printf("NDS core hardware initialized\n");
}

bool NDSCore::loadROM(const std::string& rom_path) {
    if (!loadROMFile(rom_path)) {
        return false;
    }
    
    if (!parseROMHeader()) {
        std::fprintf(stderr, "Failed to parse NDS ROM header\n");
        return false;
    }
    
    rom_path_ = rom_path;
    rom_loaded_ = true;
    
    std::printf("NDS ROM loaded: %s\n", rom_path.c_str());
    std::printf("ROM size: %zu bytes\n", rom_data_.size());
    
    // TODO: Initialize CPUs with ROM data
    // TODO: Set up memory map
    // TODO: Load ARM9 and ARM7 binaries into memory
    
    return true;
}

bool NDSCore::loadROMFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::fprintf(stderr, "Failed to open ROM file: %s\n", path.c_str());
        return false;
    }
    
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    rom_data_.resize(size);
    if (!file.read(reinterpret_cast<char*>(rom_data_.data()), size)) {
        std::fprintf(stderr, "Failed to read ROM file\n");
        return false;
    }
    
    std::printf("Loaded ROM file: %zu bytes\n", rom_data_.size());
    return true;
}

bool NDSCore::parseROMHeader() {
    if (rom_data_.size() < sizeof(NDSHeader)) {
        std::fprintf(stderr, "ROM file too small to contain valid header\n");
        return false;
    }
    
    const NDSHeader* header = reinterpret_cast<const NDSHeader*>(rom_data_.data());
    
    // Print some basic info
    char title[13] = {0};
    std::memcpy(title, header->game_title, 12);
    std::printf("Game Title: %s\n", title);
    
    char game_code[5] = {0};
    std::memcpy(game_code, header->game_code, 4);
    std::printf("Game Code: %s\n", game_code);
    
    std::printf("ARM9 ROM Offset: 0x%08X\n", header->arm9_rom_offset);
    std::printf("ARM9 Entry: 0x%08X\n", header->arm9_entry_address);
    std::printf("ARM9 RAM Address: 0x%08X\n", header->arm9_ram_address);
    std::printf("ARM9 Size: 0x%08X\n", header->arm9_size);
    std::printf("ARM7 ROM Offset: 0x%08X\n", header->arm7_rom_offset);
    std::printf("ARM7 Entry: 0x%08X\n", header->arm7_entry_address);
    std::printf("ARM7 RAM Address: 0x%08X\n", header->arm7_ram_address);
    std::printf("ARM7 Size: 0x%08X\n", header->arm7_size);
    
    // Load ROM binaries into memory
    if (!memory_->loadROM(rom_data_,
                          header->arm9_rom_offset, header->arm9_entry_address,
                          header->arm9_ram_address, header->arm9_size,
                          header->arm7_rom_offset, header->arm7_entry_address,
                          header->arm7_ram_address, header->arm7_size)) {
        std::fprintf(stderr, "Failed to load ROM binaries into memory\n");
        return false;
    }
    
    // Reset CPUs to ensure clean state, then set entry points
    arm9_->reset();
    arm7_->reset();
    
    // CRITICAL: Use the entry point from the ROM header
    // The header specifies the correct entry point, which may be in the loader or Secure Area
    // If the Secure Area is decrypted in the ROM dump, we can use the header entry point
    // Otherwise, we'll need to skip to 0x02004000
    // For now, try using the header entry point - if Secure Area is loaded, it should work
    uint32_t arm9_entry = header->arm9_entry_address;
    
    // CRITICAL: Force ARM9 entry point to 0x02004000 to skip loader/Secure Area
    // The Secure Area (0x02000800-0x02004000) contains loader code and ASCII strings
    // that can be executed as invalid instructions (e.g., 0x2106C0DE = "CODE", 0x4B44535B = "SDK[")
    // By jumping directly to 0x02004000, we skip this area and start at the actual game code
    if (arm9_entry >= 0x02000800 && arm9_entry < 0x02004000) {
        std::printf("ARM9 entry point 0x%08X is in Secure Area - forcing to 0x02004000 to skip loader\n", arm9_entry);
        arm9_entry = 0x02004000;
    } else {
        std::printf("ARM9 entry point 0x%08X is outside Secure Area - using as-is\n", arm9_entry);
    }
    
    arm9_->setEntryPoint(arm9_entry);
    arm7_->setEntryPoint(header->arm7_entry_address);
    
    // Initialize stack pointers (R13) - CRITICAL: Must be set BEFORE execution
    // ARM9: Use DTCM for stack (0x027C0000-0x027C3FFF) - private to ARM9, can't be corrupted by ARM7
    // Stack at 0x027C3F00 (near end of 16KB DTCM)
    arm9_->setRegister(13, 0x027C3F00);
    // ARM7: Stack at 0x0380FF00 (top of ARM7 private WRAM at 0x037F8000-0x03807FFF)
    // ARM7 WRAM is 64KB, so top is at 0x037F8000 + 0x10000 = 0x03808000
    // Stack should be near the top but not at the very end
    arm7_->setRegister(13, 0x0380FF00);
    
    // CRITICAL: HLE ARM7 Boot State
    // ARM7 performs a self-check on boot - if R0 and R1 aren't set correctly,
    // it will enter a panic loop. Set these to indicate successful BIOS boot
    arm7_->setRegister(0, 0x00000001);  // R0 = 1 (BIOS boot successful)
    arm7_->setRegister(1, 0x0000000F);  // R1 = 0x0F (Component ID)
    std::printf("ARM7 boot state HLE: R0=0x%08X, R1=0x%08X\n", 0x00000001, 0x0000000F);
    
    // CRITICAL: Set CPU mode based on entry point bit 0
    // setEntryPoint() already handles the Thumb bit correctly
    // Just ensure interrupts are enabled and we're in Supervisor mode
    // Don't override the mode set by setEntryPoint() - it checks bit 0 correctly
    uint32_t arm9_cpsr = arm9_->getCPSR();
    arm9_cpsr = (arm9_cpsr & ~0x1F) | 0x13;  // Supervisor mode, preserve Thumb bit
    arm9_cpsr &= ~(1 << 7);  // Enable interrupts (clear I bit)
    arm9_->setCPSR(arm9_cpsr);
    
    arm7_->setCPSR(0x13);  // Supervisor mode, ARM state, interrupts enabled
    
    std::printf("ARM9 entry point set to 0x%08X (from ROM header)\n", arm9_entry);
    
    std::printf("ROM binaries loaded, CPUs initialized\n");
    
    return true;
}

void NDSCore::reset() {
    if (!rom_loaded_) {
        return;
    }
    
    // Reset memory
    memory_->initialize();
    
    // Reload ROM
    const NDSHeader* header = reinterpret_cast<const NDSHeader*>(rom_data_.data());
    memory_->loadROM(rom_data_,
                     header->arm9_rom_offset, header->arm9_entry_address,
                     header->arm9_ram_address, header->arm9_size,
                     header->arm7_rom_offset, header->arm7_entry_address,
                     header->arm7_ram_address, header->arm7_size);
    
    // Reset CPUs
    arm9_->reset();
    arm7_->reset();
    
    // CRITICAL: Force ARM9 entry point to 0x02004000 to skip loader/Secure Area
    // The Secure Area (0x02000800-0x02004000) contains loader code and ASCII strings
    // that can be executed as invalid instructions
    uint32_t arm9_entry = header->arm9_entry_address;
    if (arm9_entry >= 0x02000800 && arm9_entry < 0x02004000) {
        arm9_entry = 0x02004000;  // Skip Secure Area
    }
    arm9_->setEntryPoint(arm9_entry);
    arm7_->setEntryPoint(header->arm7_entry_address);
    
    // Initialize stack pointers (R13) - critical for function calls
    // ARM9: Use DTCM for stack (0x027C0000-0x027C3FFF) - private to ARM9, can't be corrupted by ARM7
    arm9_->setRegister(13, 0x027C3F00);
    arm7_->setRegister(13, 0x0380FF00);
    
    // CRITICAL: HLE ARM7 Boot State
    // ARM7 performs a self-check on boot - if R0 and R1 aren't set correctly,
    // it will enter a panic loop. Set these to indicate successful BIOS boot
    arm7_->setRegister(0, 0x00000001);  // R0 = 1 (BIOS boot successful)
    arm7_->setRegister(1, 0x0000000F);  // R1 = 0x0F (Component ID)
    
    // CRITICAL: Set CPU mode based on entry point bit 0
    // setEntryPoint() already handles the Thumb bit correctly
    // Just ensure interrupts are enabled and we're in Supervisor mode
    // Don't override the mode set by setEntryPoint() - it checks bit 0 correctly
    uint32_t arm9_cpsr = arm9_->getCPSR();
    arm9_cpsr = (arm9_cpsr & ~0x1F) | 0x13;  // Supervisor mode, preserve Thumb bit
    arm9_cpsr &= ~(1 << 7);  // Enable interrupts (clear I bit)
    arm9_->setCPSR(arm9_cpsr);
    
    arm7_->setCPSR(0x13);  // Supervisor mode, ARM state, interrupts enabled
    
    cycles_this_frame_ = 0;
    
    std::printf("NDS core reset\n");
}

void NDSCore::runFrame() {
    if (!rom_loaded_) {
        return;
    }
    
    // Run one frame of emulation
    runFrameCycles();
    
    // Render graphics
    if (ppu_) {
        ppu_->renderFrame();
    }
}

void NDSCore::runFrameCycles() {
    cycles_this_frame_ = 0;
    
    // Update PPU scanline timing during frame execution
    // Each frame is ~33.5M cycles, with 262 scanlines
    // Each scanline is ~128K cycles
    constexpr uint32_t CYCLES_PER_SCANLINE = 33554432 / 262;  // ~128K cycles per scanline
    
    // Run ARM9 and ARM7 CPUs
    // ARM9 runs at 67MHz, ARM7 runs at 33MHz
    // For simplicity, we'll run them in a 2:1 ratio
    while (cycles_this_frame_ < cycles_per_frame_) {
        // Update PPU scanline based on cycles elapsed
        if (ppu_) {
            uint32_t scanline_num = cycles_this_frame_ / CYCLES_PER_SCANLINE;
            if (scanline_num >= 262) {
                scanline_num = 261;  // Clamp to last scanline
            }
            ppu_->updateScanline(scanline_num);
        }
        
        // Update timers
        arm9_timers_->step(2);
        arm7_timers_->step(1);
        
        // Process DMA transfers
        if (dma_) {
            dma_->processTransfers();
        }
        
        // Update ROM/Game Card state
        if (memory_) {
            memory_->updateROMState(2);  // Update with 2 cycles (ARM9 speed)
        }
        
        // Run ARM9 (main CPU)
        arm9_->step();
        cycles_this_frame_ += 2;  // ARM9 runs at 2x speed
        
        // Run ARM7 every other cycle
        if ((cycles_this_frame_ % 4) == 0) {
            arm7_->step();
        }
        
        // Limit execution to prevent infinite loops
        if (cycles_this_frame_ > cycles_per_frame_ * 2) {
            std::fprintf(stderr, "Warning: Frame execution exceeded cycle limit\n");
            break;
        }
    }
    
    // VBlank interrupt is now generated by PPU::renderFrame()
}

void NDSCore::handleEvent(const SDL_Event& event) {
    if (!frontend_) {
        return;
    }
    
    // Get input state from frontend and update core's input registers
    // TODO: Map frontend input to NDS input registers
    // For now, this is a placeholder
}

void NDSCore::setFrontend(Frontend* frontend) {
    frontend_ = frontend;
}

bool NDSCore::saveState(const std::string& filepath) {
    // TODO: Implement save state serialization
    std::fprintf(stderr, "Save states not yet implemented\n");
    return false;
}

bool NDSCore::loadState(const std::string& filepath) {
    // TODO: Implement save state loading
    std::fprintf(stderr, "Save states not yet implemented\n");
    return false;
}
