#include "bios_hle.hpp"
#include "memory.hpp"
#include "arm_cpu.hpp"
#include "interrupts.hpp"
#include "nds_core.hpp"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <unordered_set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

BIOSHLE::BIOSHLE(NDSMemory* memory)
    : memory_(memory)
    , arm9_interrupts_(nullptr)
    , arm7_interrupts_(nullptr)
    , core_(nullptr)
{
}

BIOSHLE::~BIOSHLE() {
}

bool BIOSHLE::handleSWI(ARMCpu* cpu, uint32_t swi_number) {
    // Determine if this is ARM9 or ARM7 based on SWI number ranges
    // For now, we'll handle both similarly
    
    // Common SWI numbers (same for both CPUs)
    // Standard BIOS SWIs (0x00-0x18)
    if (swi_number <= 0x18) {
        switch (swi_number) {
            case 0x00: swi_SoftReset(cpu); return true;
            case 0x01: swi_RegisterRamReset(cpu); return true;
            case 0x02: swi_Halt(cpu); return true;
            case 0x03: swi_Stop(cpu); return true;
            case 0x04: swi_IntrWait(cpu); return true;
            case 0x05: swi_VBlankIntrWait(cpu); return true;
            case 0x06: swi_Div(cpu); return true;
            case 0x07: swi_DivArm(cpu); return true;
            case 0x08: swi_Sqrt(cpu); return true;
            case 0x09: swi_ArcTan(cpu); return true;
            case 0x0A: swi_ArcTan2(cpu); return true;
            case 0x0B: swi_CpuSet(cpu); return true;
            case 0x0C: swi_CpuFastSet(cpu); return true;
            case 0x0D: swi_GetBiosChecksum(cpu); return true;
            case 0x0E: swi_BgAffineSet(cpu); return true;
            case 0x0F: swi_ObjAffineSet(cpu); return true;
            case 0x10: swi_BitUnPack(cpu); return true;
            case 0x11: swi_LZ77UnCompReadNormalWrite8bit(cpu); return true;
            case 0x12: swi_LZ77UnCompReadNormalWrite16bit(cpu); return true;
            case 0x13: swi_HuffUnCompReadNormal(cpu); return true;
            case 0x14: swi_RLUnCompReadNormalWrite8bit(cpu); return true;
            case 0x15: swi_RLUnCompReadNormalWrite16bit(cpu); return true;
            case 0x16: swi_Diff8bitUnFilterWrite8bit(cpu); return true;
            case 0x17: swi_Diff8bitUnFilterWrite16bit(cpu); return true;
            case 0x18: swi_Diff16bitUnFilter(cpu); return true;
        }
    }
    
    // Game-specific or custom SWIs - many games use custom SWI numbers
    // SWI 0xFF0000 is commonly used by games for various purposes
    // Some games use it as a no-op, others use it for synchronization
    if (swi_number == 0xFF0000) {
        // Common game SWI - just return (no-op)
        return true;
    }
    // High SWI numbers (0xFCxxxx, 0xFDxxxx, etc.) are often cartridge-related or custom game SWIs
    // Handle them as no-ops to prevent crashes
    if (swi_number >= 0xFC0000) {
        // These are likely custom game SWIs or cartridge-related - ignore them silently
        return true;  // Pretend we handled it
    }
    
    // Log unknown SWIs
    static std::unordered_set<uint32_t> logged_swis;
    if (logged_swis.size() < 50) {
        if (logged_swis.find(swi_number) == logged_swis.end()) {
            logged_swis.insert(swi_number);
            std::printf("Unhandled SWI 0x%06X (PC=0x%08X)\n", swi_number, cpu->getPC());
        }
    }
    return false;
}

bool BIOSHLE::isSWIImplemented(uint32_t swi_number) const {
    return swi_number <= 0x18;
}

void BIOSHLE::setInterruptController(InterruptController* interrupts, bool is_arm9) {
    if (is_arm9) {
        arm9_interrupts_ = interrupts;
    } else {
        arm7_interrupts_ = interrupts;
    }
}

void BIOSHLE::setCore(class NDSCore* core) {
    core_ = core;
}

void BIOSHLE::swi_SoftReset(ARMCpu* cpu) {
    // Soft reset - reset the system
    std::printf("SWI SoftReset called (PC=0x%08X)\n", cpu->getPC());
    if (core_) {
        core_->reset();
    }
}

void BIOSHLE::swi_RegisterRamReset(ARMCpu* cpu) {
    // Register RAM reset - clear certain memory regions
    // For now, just log it
    std::printf("SWI RegisterRamReset called\n");
}

void BIOSHLE::swi_Halt(ARMCpu* cpu) {
    // Halt CPU - wait for interrupt
    // For now, just return (we'll implement proper interrupt handling later)
    std::printf("SWI Halt called\n");
}

void BIOSHLE::swi_Stop(ARMCpu* cpu) {
    // Stop CPU - low power mode
    // For now, just return
    std::printf("SWI Stop called\n");
}

void BIOSHLE::swi_IntrWait(ARMCpu* cpu) {
    // Wait for interrupt
    // For now, just return immediately
    std::printf("SWI IntrWait called\n");
}

void BIOSHLE::swi_VBlankIntrWait(ARMCpu* cpu) {
    // Wait for VBlank interrupt
    // This is critical for games - they wait here for each frame
    // R0 = flags (bit 0 = clear flag 0, bit 1 = clear flag 1)
    // R1 = flags to wait for
    
    uint32_t flags = cpu->getRegister(0);
    uint32_t wait_flags = cpu->getRegister(1);
    
    // Determine which CPU this is (ARM9 or ARM7)
    InterruptController* interrupts = nullptr;
    bool is_arm9 = cpu->getPC() >= 0x02000000;
    if (is_arm9) {
        interrupts = arm9_interrupts_;
    } else {
        interrupts = arm7_interrupts_;
    }
    
    if (!interrupts) {
        return;  // No interrupt controller, just return
    }
    
    // Check if VBlank interrupt is enabled
    uint32_t ie = interrupts->readIE();
    bool vblank_enabled = (ie & (1 << InterruptController::INT_LCD_VBLANK)) != 0;
    
    // Check if VBlank interrupt is pending
    uint32_t if_reg = interrupts->readIF();
    bool vblank_pending = (if_reg & (1 << InterruptController::INT_LCD_VBLANK)) != 0;
    
    // Log first few calls to see what's happening
    static int vblank_wait_count = 0;
    if (vblank_wait_count < 5) {
        std::printf("%s: VBlankIntrWait called (IE=0x%08X, IF=0x%08X, enabled=%d, pending=%d)\n",
                   is_arm9 ? "ARM9" : "ARM7", ie, if_reg, vblank_enabled, vblank_pending);
        vblank_wait_count++;
    }
    
    if (vblank_enabled && vblank_pending) {
        // VBlank occurred, clear the interrupt flag and return
        interrupts->writeIF(1 << InterruptController::INT_LCD_VBLANK);
        return;
    }
    
    // If VBlank is enabled but not pending, halt the CPU until VBlank occurs
    // The PPU will trigger the interrupt when VBlank happens, which will wake up the CPU
    if (vblank_enabled && !vblank_pending) {
        // Halt the CPU - it will be woken up when VBlank interrupt is triggered
        cpu->setHalted(true);
        return;
    }
    
    // If VBlank is not enabled yet, just return
    // The game will enable interrupts and call this again
}

void BIOSHLE::swi_Div(ARMCpu* cpu) {
    // Division: R0 = R1 / R2, R3 = R1 % R2
    int32_t numerator = static_cast<int32_t>(cpu->getRegister(1));
    int32_t denominator = static_cast<int32_t>(cpu->getRegister(2));
    
    if (denominator == 0) {
        cpu->setRegister(0, 0);
        cpu->setRegister(3, 0);
    } else {
        cpu->setRegister(0, numerator / denominator);
        cpu->setRegister(3, numerator % denominator);
    }
}

void BIOSHLE::swi_DivArm(ARMCpu* cpu) {
    // ARM division: R0 = R0 / R1
    int32_t numerator = static_cast<int32_t>(cpu->getRegister(0));
    int32_t denominator = static_cast<int32_t>(cpu->getRegister(1));
    
    if (denominator == 0) {
        cpu->setRegister(0, 0);
    } else {
        cpu->setRegister(0, numerator / denominator);
    }
}

void BIOSHLE::swi_Sqrt(ARMCpu* cpu) {
    // Square root: R0 = sqrt(R0)
    uint32_t value = cpu->getRegister(0);
    uint32_t result = static_cast<uint32_t>(std::sqrt(static_cast<double>(value)));
    cpu->setRegister(0, result);
}

void BIOSHLE::swi_ArcTan(ARMCpu* cpu) {
    // Arctangent: R0 = atan(R0) * 32768 / PI
    int16_t value = static_cast<int16_t>(cpu->getRegister(0) & 0xFFFF);
    double atan_result = std::atan(static_cast<double>(value) / 32768.0);
    int16_t result = static_cast<int16_t>((atan_result * 32768.0) / M_PI);
    cpu->setRegister(0, static_cast<uint32_t>(static_cast<int16_t>(result)));
}

void BIOSHLE::swi_ArcTan2(ARMCpu* cpu) {
    // Arctangent2: R0 = atan2(R0, R1) * 32768 / PI
    int16_t y = static_cast<int16_t>(cpu->getRegister(0) & 0xFFFF);
    int16_t x = static_cast<int16_t>(cpu->getRegister(1) & 0xFFFF);
    double atan2_result = std::atan2(static_cast<double>(y), static_cast<double>(x));
    int16_t result = static_cast<int16_t>((atan2_result * 32768.0) / M_PI);
    cpu->setRegister(0, static_cast<uint32_t>(static_cast<int16_t>(result)));
}

void BIOSHLE::swi_CpuSet(ARMCpu* cpu) {
    // CPU Set: Copy/fill memory
    // R0 = source, R1 = destination, R2 = control
    uint32_t source = cpu->getRegister(0);
    uint32_t dest = cpu->getRegister(1);
    uint32_t control = cpu->getRegister(2);
    
    uint32_t count = control & 0x001FFFFF;
    bool word_transfer = (control >> 26) & 1;
    bool fill = (control >> 24) & 1;
    
    // Determine which CPU this is (ARM9 or ARM7) based on PC
    bool is_arm9 = cpu->getPC() >= 0x02000000;
    
    if (word_transfer) {
        // 32-bit transfer
        count *= 4;
        if (fill) {
            uint32_t value = is_arm9 ? memory_->read32_ARM9(source) : memory_->read32_ARM7(source);
            for (uint32_t i = 0; i < count; i += 4) {
                if (is_arm9) {
                    memory_->write32_ARM9(dest + i, value);
                } else {
                    memory_->write32_ARM7(dest + i, value);
                }
            }
        } else {
            for (uint32_t i = 0; i < count; i += 4) {
                uint32_t value = is_arm9 ? memory_->read32_ARM9(source + i) : memory_->read32_ARM7(source + i);
                if (is_arm9) {
                    memory_->write32_ARM9(dest + i, value);
                } else {
                    memory_->write32_ARM7(dest + i, value);
                }
            }
        }
    } else {
        // 16-bit transfer
        count *= 2;
        if (fill) {
            uint16_t value = is_arm9 ? memory_->read16_ARM9(source) : memory_->read16_ARM7(source);
            for (uint32_t i = 0; i < count; i += 2) {
                if (is_arm9) {
                    memory_->write16_ARM9(dest + i, value);
                } else {
                    memory_->write16_ARM7(dest + i, value);
                }
            }
        } else {
            for (uint32_t i = 0; i < count; i += 2) {
                uint16_t value = is_arm9 ? memory_->read16_ARM9(source + i) : memory_->read16_ARM7(source + i);
                if (is_arm9) {
                    memory_->write16_ARM9(dest + i, value);
                } else {
                    memory_->write16_ARM7(dest + i, value);
                }
            }
        }
    }
}

void BIOSHLE::swi_CpuFastSet(ARMCpu* cpu) {
    // CPU Fast Set: Fast copy/fill (32-bit only)
    // R0 = source, R1 = destination, R2 = control
    uint32_t source = cpu->getRegister(0);
    uint32_t dest = cpu->getRegister(1);
    uint32_t control = cpu->getRegister(2);
    
    uint32_t count = (control & 0x001FFFFF) * 4;  // Always 32-bit, count in words
    bool fill = (control >> 24) & 1;
    
    // Determine which CPU this is (ARM9 or ARM7) based on PC
    bool is_arm9 = cpu->getPC() >= 0x02000000;
    
    if (fill) {
        uint32_t value = is_arm9 ? memory_->read32_ARM9(source) : memory_->read32_ARM7(source);
        for (uint32_t i = 0; i < count; i += 4) {
            if (is_arm9) {
                memory_->write32_ARM9(dest + i, value);
            } else {
                memory_->write32_ARM7(dest + i, value);
            }
        }
    } else {
        for (uint32_t i = 0; i < count; i += 4) {
            uint32_t value = is_arm9 ? memory_->read32_ARM9(source + i) : memory_->read32_ARM7(source + i);
            if (is_arm9) {
                memory_->write32_ARM9(dest + i, value);
            } else {
                memory_->write32_ARM7(dest + i, value);
            }
        }
    }
}

void BIOSHLE::swi_GetBiosChecksum(ARMCpu* cpu) {
    // Get BIOS checksum - return a dummy value
    cpu->setRegister(0, 0xBAAE187F);  // Common NDS BIOS checksum
    cpu->setRegister(1, 0x1EED0A0F);
}

void BIOSHLE::swi_BgAffineSet(ARMCpu* cpu) {
    // Background affine transformation setup
    // R0 = source, R1 = destination, R2 = count
    // TODO: Implement properly
    std::printf("SWI BgAffineSet called (not fully implemented)\n");
}

void BIOSHLE::swi_ObjAffineSet(ARMCpu* cpu) {
    // Object affine transformation setup
    // R0 = source, R1 = destination, R2 = count, R3 = offset
    // TODO: Implement properly
    std::printf("SWI ObjAffineSet called (not fully implemented)\n");
}

void BIOSHLE::swi_BitUnPack(ARMCpu* cpu) {
    // Bit unpacking
    // TODO: Implement
    std::printf("SWI BitUnPack called (not implemented)\n");
}

void BIOSHLE::swi_LZ77UnCompReadNormalWrite8bit(ARMCpu* cpu) {
    // LZ77 decompression (8-bit write)
    // R0 = source address, R1 = destination address
    uint32_t source = cpu->getRegister(0);
    uint32_t dest = cpu->getRegister(1);
    
    bool is_arm9 = cpu->getPC() >= 0x02000000;
    
    // CRITICAL: Log LZ77 calls to verify sub-kernel loading
    static int lz77_call_count = 0;
    if (lz77_call_count < 10) {
        std::printf("%s: LZ77 decompression called - Source=0x%08X, Dest=0x%08X (PC=0x%08X)\n",
                   is_arm9 ? "ARM9" : "ARM7", source, dest, cpu->getPC());
        lz77_call_count++;
    }
    
    // Read header
    uint32_t header = is_arm9 ? memory_->read32_ARM9(source) : memory_->read32_ARM7(source);
    source += 4;
    
    // LZ77 header format: byte 0 = type (0x10), bytes 1-3 = size (24-bit)
    uint32_t uncompressed_size = (header >> 8) & 0x00FFFFFF;  // 24-bit size
    uint8_t compression_type = header & 0xFF;
    
    if (compression_type != 0x10) {
        std::fprintf(stderr, "LZ77: Invalid compression type 0x%02X\n", compression_type);
        return;
    }
    
    // Log if decompressing to Shared WRAM (sub-kernel area)
    if (dest >= 0x03000000 && dest < 0x04000000 && lz77_call_count <= 10) {
        std::printf("%s: LZ77 decompressing %u bytes to Shared WRAM at 0x%08X (sub-kernel area)\n",
                   is_arm9 ? "ARM9" : "ARM7", uncompressed_size, dest);
    }
    
    uint32_t bytes_written = 0;
    uint8_t flags = 0;
    uint8_t flags_remaining = 0;
    
    while (bytes_written < uncompressed_size) {
        if (flags_remaining == 0) {
            flags = is_arm9 ? memory_->read8_ARM9(source) : memory_->read8_ARM7(source);
            source++;
            flags_remaining = 8;
        }
        
        if (flags & 0x80) {
            // Compressed: read 16-bit value
            uint16_t data = is_arm9 ? memory_->read16_ARM9(source) : memory_->read16_ARM7(source);
            source += 2;
            
            uint32_t length = ((data >> 4) & 0x0F) + 3;
            uint32_t disp = (data & 0x0FFF) + 1;
            
            // Copy from destination buffer
            for (uint32_t i = 0; i < length && bytes_written < uncompressed_size; i++) {
                uint32_t copy_addr = dest + bytes_written - disp;
                uint8_t byte = is_arm9 ? memory_->read8_ARM9(copy_addr) : memory_->read8_ARM7(copy_addr);
                if (is_arm9) {
                    memory_->write8_ARM9(dest + bytes_written, byte);
                } else {
                    memory_->write8_ARM7(dest + bytes_written, byte);
                }
                bytes_written++;
            }
        } else {
            // Uncompressed: copy byte directly
            uint8_t byte = is_arm9 ? memory_->read8_ARM9(source) : memory_->read8_ARM7(source);
            source++;
            if (is_arm9) {
                memory_->write8_ARM9(dest + bytes_written, byte);
            } else {
                memory_->write8_ARM7(dest + bytes_written, byte);
            }
            bytes_written++;
        }
        
        flags <<= 1;
        flags_remaining--;
    }
}

void BIOSHLE::swi_LZ77UnCompReadNormalWrite16bit(ARMCpu* cpu) {
    // LZ77 decompression (16-bit write)
    // R0 = source address, R1 = destination address
    uint32_t source = cpu->getRegister(0);
    uint32_t dest = cpu->getRegister(1);
    
    bool is_arm9 = cpu->getPC() >= 0x02000000;
    
    // CRITICAL: Log LZ77 calls to verify sub-kernel loading
    static int lz77_call_count = 0;
    if (lz77_call_count < 10) {
        std::printf("%s: LZ77 decompression (16-bit) called - Source=0x%08X, Dest=0x%08X (PC=0x%08X)\n",
                   is_arm9 ? "ARM9" : "ARM7", source, dest, cpu->getPC());
        lz77_call_count++;
    }
    
    // Read header
    uint32_t header = is_arm9 ? memory_->read32_ARM9(source) : memory_->read32_ARM7(source);
    source += 4;
    
    // LZ77 header format: byte 0 = type (0x10), bytes 1-3 = size (24-bit)
    uint32_t uncompressed_size = (header >> 8) & 0x00FFFFFF;  // 24-bit size
    uint8_t compression_type = header & 0xFF;
    
    if (compression_type != 0x10) {
        std::fprintf(stderr, "LZ77: Invalid compression type 0x%02X\n", compression_type);
        return;
    }
    
    // Log if decompressing to Shared WRAM (sub-kernel area)
    if (dest >= 0x03000000 && dest < 0x04000000 && lz77_call_count <= 10) {
        std::printf("%s: LZ77 decompressing %u bytes to Shared WRAM at 0x%08X (sub-kernel area)\n",
                   is_arm9 ? "ARM9" : "ARM7", uncompressed_size, dest);
    }
    
    uint32_t bytes_written = 0;
    uint8_t flags = 0;
    uint8_t flags_remaining = 0;
    
    while (bytes_written < uncompressed_size) {
        if (flags_remaining == 0) {
            flags = is_arm9 ? memory_->read8_ARM9(source) : memory_->read8_ARM7(source);
            source++;
            flags_remaining = 8;
        }
        
        if (flags & 0x80) {
            // Compressed: read 16-bit value
            uint16_t data = is_arm9 ? memory_->read16_ARM9(source) : memory_->read16_ARM7(source);
            source += 2;
            
            uint32_t length = ((data >> 4) & 0x0F) + 3;
            uint32_t disp = (data & 0x0FFF) + 1;
            
            // Copy from destination buffer (16-bit aligned)
            for (uint32_t i = 0; i < length && bytes_written < uncompressed_size; i++) {
                uint32_t copy_addr = dest + bytes_written - (disp * 2);
                uint16_t halfword = is_arm9 ? memory_->read16_ARM9(copy_addr) : memory_->read16_ARM7(copy_addr);
                if (is_arm9) {
                    memory_->write16_ARM9(dest + bytes_written, halfword);
                } else {
                    memory_->write16_ARM7(dest + bytes_written, halfword);
                }
                bytes_written += 2;
            }
        } else {
            // Uncompressed: copy halfword directly
            uint16_t halfword = is_arm9 ? memory_->read16_ARM9(source) : memory_->read16_ARM7(source);
            source += 2;
            if (is_arm9) {
                memory_->write16_ARM9(dest + bytes_written, halfword);
            } else {
                memory_->write16_ARM7(dest + bytes_written, halfword);
            }
            bytes_written += 2;
        }
        
        flags <<= 1;
        flags_remaining--;
    }
}

void BIOSHLE::swi_HuffUnCompReadNormal(ARMCpu* cpu) {
    // Huffman decompression
    // TODO: Implement
    std::printf("SWI HuffUnComp called (not implemented)\n");
}

void BIOSHLE::swi_RLUnCompReadNormalWrite8bit(ARMCpu* cpu) {
    // Run-length decompression (8-bit)
    // TODO: Implement
    std::printf("SWI RLUnComp (8-bit) called (not implemented)\n");
}

void BIOSHLE::swi_RLUnCompReadNormalWrite16bit(ARMCpu* cpu) {
    // Run-length decompression (16-bit)
    // TODO: Implement
    std::printf("SWI RLUnComp (16-bit) called (not implemented)\n");
}

void BIOSHLE::swi_Diff8bitUnFilterWrite8bit(ARMCpu* cpu) {
    // Differential filter (8-bit)
    // TODO: Implement
    std::printf("SWI Diff8bitUnFilter (8-bit) called (not implemented)\n");
}

void BIOSHLE::swi_Diff8bitUnFilterWrite16bit(ARMCpu* cpu) {
    // Differential filter (8-bit to 16-bit)
    // TODO: Implement
    std::printf("SWI Diff8bitUnFilter (16-bit) called (not implemented)\n");
}

void BIOSHLE::swi_Diff16bitUnFilter(ARMCpu* cpu) {
    // Differential filter (16-bit)
    // TODO: Implement
    std::printf("SWI Diff16bitUnFilter called (not implemented)\n");
}
