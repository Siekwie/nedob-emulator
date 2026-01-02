#include "dma.hpp"
#include "memory.hpp"
#include "interrupts.hpp"
#include <cstdio>
#include <cstring>

DMA::DMA()
    : memory_(nullptr)
    , arm9_interrupts_(nullptr)
    , arm7_interrupts_(nullptr)
{
    // Initialize all channels
    for (int i = 0; i < 4; i++) {
        arm9_channels_[i] = {0, 0, 0, false, 0};
        arm7_channels_[i] = {0, 0, 0, false, 0};
    }
}

DMA::~DMA() {
}

void DMA::setMemory(NDSMemory* memory) {
    memory_ = memory;
}

void DMA::setInterruptController(InterruptController* interrupts, bool is_arm9) {
    if (is_arm9) {
        arm9_interrupts_ = interrupts;
    } else {
        arm7_interrupts_ = interrupts;
    }
}

uint32_t DMA::readDMA_SAD(uint32_t channel, bool is_arm9) {
    if (channel >= 4) return 0;
    DMAChannel& ch = is_arm9 ? arm9_channels_[channel] : arm7_channels_[channel];
    return ch.source_addr;
}

uint32_t DMA::readDMA_DAD(uint32_t channel, bool is_arm9) {
    if (channel >= 4) return 0;
    DMAChannel& ch = is_arm9 ? arm9_channels_[channel] : arm7_channels_[channel];
    return ch.dest_addr;
}

uint32_t DMA::readDMA_CNT(uint32_t channel, bool is_arm9) {
    if (channel >= 4) return 0;
    DMAChannel& ch = is_arm9 ? arm9_channels_[channel] : arm7_channels_[channel];
    // Return control register, with active bit cleared if transfer is done
    // CRITICAL: Bit 31 = Enable/Busy (1 = busy, 0 = not busy)
    // Games check this bit to see if DMA is complete - must return 0 when not active
    uint32_t cnt = ch.control;
    if (!ch.active) {
        cnt &= ~(1U << 31);  // Clear enable/busy bit (bit 31)
    }
    // Ensure bit 31 is 0 when not active (games wait for this)
    return cnt;
}

void DMA::writeDMA_SAD(uint32_t channel, bool is_arm9, uint32_t value) {
    if (channel >= 4) return;
    DMAChannel& ch = is_arm9 ? arm9_channels_[channel] : arm7_channels_[channel];
    ch.source_addr = value & 0x07FFFFFF;  // 27-bit address
}

void DMA::writeDMA_DAD(uint32_t channel, bool is_arm9, uint32_t value) {
    if (channel >= 4) return;
    DMAChannel& ch = is_arm9 ? arm9_channels_[channel] : arm7_channels_[channel];
    ch.dest_addr = value & 0x07FFFFFF;  // 27-bit address
}

void DMA::writeDMA_CNT(uint32_t channel, bool is_arm9, uint32_t value) {
    if (channel >= 4) return;
    DMAChannel& ch = is_arm9 ? arm9_channels_[channel] : arm7_channels_[channel];
    
    uint32_t old_control = ch.control;
    ch.control = value;
    
    // Check if DMA is being enabled
    bool was_enabled = (old_control >> 31) & 1;
    bool now_enabled = (value >> 31) & 1;
    
    if (!was_enabled && now_enabled) {
        // DMA just enabled - start transfer
        uint32_t count = (value >> 0) & 0x1FFFFF;  // 21-bit count
        if (count == 0) {
            count = 0x200000;  // 0 means 2MB
        }
        ch.remaining_count = count;
        ch.active = true;
        
        // Execute transfer immediately (for immediate mode)
        uint32_t mode = (value >> 27) & 0x7;
        if (mode == 0) {  // Immediate transfer
            // Log first few DMA transfers for debugging
            static int dma_log_count = 0;
            uint32_t channel_num = channel;  // Use the channel parameter directly
            
            // CRITICAL: Always log DMA3 (channel 3) as it's used for sub-kernel loading
            bool should_log = (channel_num == 3) || (dma_log_count < 5);
            
            if (should_log) {
                uint32_t transfer_type = (value >> 26) & 0x1;  // 0=16-bit, 1=32-bit
                uint32_t bytes = count * (transfer_type ? 4 : 2);
                std::printf("%s: DMA%d enabled - Src=0x%08X, Dest=0x%08X, Count=0x%X (%u bytes), Type=%s, Mode=0 (Immediate)\n",
                           is_arm9 ? "ARM9" : "ARM7", channel_num, ch.source_addr, ch.dest_addr, count, bytes,
                           transfer_type ? "32-bit" : "16-bit");
                if (channel_num != 3) {
                    dma_log_count++;
                }
            }
            executeTransfer(ch, is_arm9);
            
            // Log completion
            if (should_log) {
                std::printf("%s: DMA%d transfer completed\n", is_arm9 ? "ARM9" : "ARM7", channel_num);
            }
        }
    }
}

void DMA::processTransfers() {
    // Process ARM9 DMA channels
    for (uint32_t i = 0; i < 4; i++) {
        DMAChannel& ch = arm9_channels_[i];
        if (ch.active) {
            uint32_t mode = (ch.control >> 27) & 0x7;
            // For now, only handle immediate transfers
            // Other modes (VBlank, HBlank, etc.) need timing
            if (mode == 0) {
                // Already executed in writeDMA_CNT
                // But check if it needs to continue
            }
        }
    }
    
    // Process ARM7 DMA channels
    for (uint32_t i = 0; i < 4; i++) {
        DMAChannel& ch = arm7_channels_[i];
        if (ch.active) {
            uint32_t mode = (ch.control >> 27) & 0x7;
            if (mode == 0) {
                // Already executed in writeDMA_CNT
            }
        }
    }
}

void DMA::executeTransfer(DMAChannel& channel, bool is_arm9) {
    if (!memory_ || !channel.active) return;
    
    uint32_t count = channel.remaining_count;
    if (count == 0) return;
    
    // CRITICAL: Log ROM-to-RAM transfers (game engine loading)
    if (channel.source_addr >= 0x08000000 && channel.dest_addr >= 0x02000000 && channel.dest_addr < 0x03000000) {
        static int rom_to_ram_log = 0;
        if (rom_to_ram_log < 3) {
            std::printf("DMA: ROM->RAM transfer detected! Src=0x%08X, Dest=0x%08X, Count=0x%X\n",
                       channel.source_addr, channel.dest_addr, count);
            rom_to_ram_log++;
        }
    }
    
    // CRITICAL: Log ROM-to-Shared WRAM transfers (sub-kernel loading)
    if (channel.source_addr >= 0x08000000 && channel.dest_addr >= 0x03000000 && channel.dest_addr < 0x04000000) {
        static int rom_to_wram_log = 0;
        if (rom_to_wram_log < 5) {
            std::printf("DMA: ROM->Shared WRAM transfer detected! Src=0x%08X, Dest=0x%08X, Count=0x%X (sub-kernel area)\n",
                       channel.source_addr, channel.dest_addr, count);
            rom_to_wram_log++;
        }
    }
    
    // Get transfer parameters
    uint32_t dest_control = (channel.control >> 21) & 0x3;
    uint32_t src_control = (channel.control >> 23) & 0x3;
    uint32_t repeat = (channel.control >> 25) & 0x1;
    uint32_t transfer_type = (channel.control >> 26) & 0x1;  // 0=16-bit, 1=32-bit
    uint32_t dest_addr = channel.dest_addr;
    uint32_t src_addr = channel.source_addr;
    
    // Determine transfer size
    uint32_t transfer_size = transfer_type ? 4 : 2;
    uint32_t addr_inc = transfer_size;
    
    // Handle address control
    if (src_control == 2) {  // Fixed source
        addr_inc = 0;
    } else if (src_control == 3) {  // Reload (for repeat mode)
        // Will reload at end
    }
    
    if (dest_control == 2) {  // Fixed destination
        addr_inc = 0;
    } else if (dest_control == 3) {  // Reload (for repeat mode)
        // Will reload at end
    }
    
    // Perform transfer - complete it immediately for immediate mode
    // Remove the byte limit to ensure full transfer completes
    uint32_t bytes_transferred = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (transfer_type) {
            // 32-bit transfer
            uint32_t value = is_arm9 ? memory_->read32_ARM9(src_addr) : memory_->read32_ARM7(src_addr);
            if (is_arm9) {
                memory_->write32_ARM9(dest_addr, value);
            } else {
                memory_->write32_ARM7(dest_addr, value);
            }
        } else {
            // 16-bit transfer
            uint16_t value = is_arm9 ? memory_->read16_ARM9(src_addr) : memory_->read16_ARM7(src_addr);
            if (is_arm9) {
                memory_->write16_ARM9(dest_addr, value);
            } else {
                memory_->write16_ARM7(dest_addr, value);
            }
        }
        
        // Update addresses
        if (src_control == 0 || src_control == 1) {  // Increment
            src_addr += addr_inc;
        }
        if (dest_control == 0 || dest_control == 1) {  // Increment
            dest_addr += addr_inc;
        }
        
        bytes_transferred += transfer_size;
    }
    
    // Update channel state
    channel.remaining_count -= (bytes_transferred / transfer_size);
    channel.source_addr = src_addr;
    channel.dest_addr = dest_addr;
    
    if (channel.remaining_count == 0) {
        // Transfer complete
        channel.active = false;
        channel.control &= ~(1 << 31);  // Clear enable bit
        
        // Trigger interrupt if enabled
        if ((channel.control >> 30) & 1) {
            // Find which channel this is
            uint32_t channel_num = 0;
            for (uint32_t i = 0; i < 4; i++) {
                DMAChannel* ch = is_arm9 ? &arm9_channels_[i] : &arm7_channels_[i];
                if (ch == &channel) {
                    channel_num = i;
                    break;
                }
            }
            triggerInterrupt(channel_num, is_arm9);
        }
    }
}

void DMA::triggerInterrupt(uint32_t channel, bool is_arm9) {
    InterruptController* interrupts = is_arm9 ? arm9_interrupts_ : arm7_interrupts_;
    if (!interrupts) return;
    
    // Map channel to interrupt type
    InterruptController::InterruptType int_type;
    switch (channel) {
        case 0: int_type = InterruptController::INT_DMA0; break;
        case 1: int_type = InterruptController::INT_DMA1; break;
        case 2: int_type = InterruptController::INT_DMA2; break;
        case 3: int_type = InterruptController::INT_DMA3; break;
        default: return;
    }
    
    interrupts->requestInterrupt(int_type);
}
