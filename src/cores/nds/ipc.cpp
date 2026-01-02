#include "ipc.hpp"
#include "interrupts.hpp"
#include <cstdio>

IPC::IPC()
    : fifo_ctrl_arm9_(0x4101)  // Default: FIFO enabled (bit 14), Receive Empty (bit 8), Send Empty (bit 0)
    , fifo_ctrl_arm7_(0x4101)  // Default: FIFO enabled (bit 14), Receive Empty (bit 8), Send Empty (bit 0)
    , arm9_interrupts_(nullptr)
    , arm7_interrupts_(nullptr)
    , ipcsync_(0)
{
    // CRITICAL: Initialize FIFO control to expected handshake value
    // Bit 0: Send FIFO Empty = 1
    // Bit 8: Receive FIFO Empty = 1
    // Bit 14: FIFO Enable = 1
    // This gives 0x4000 | 0x0100 | 0x0001 = 0x4101
}

IPC::~IPC() {
}

void IPC::setInterruptController(InterruptController* interrupts, bool is_arm9) {
    if (is_arm9) {
        arm9_interrupts_ = interrupts;
    } else {
        arm7_interrupts_ = interrupts;
    }
}

uint32_t IPC::readFIFOSend(bool is_arm9) const {
    // FIFO Send register: reading from this returns the value you wrote
    // This is typically used to check what was sent
    // For proper FIFO behavior, reading should return the last value written
    // But in practice, games read from FIFO Receive to get data
    return 0;  // Send register is write-only for the sender
}

uint32_t IPC::readFIFORecv(bool is_arm9) {
    // FIFO Receive register: returns value from the other CPU's FIFO
    // Reading from this pops a value from the receive FIFO
    if (is_arm9) {
        // ARM9 reading from ARM7's FIFO
        if (!fifo_arm7_to_arm9_.empty()) {
            uint32_t value = fifo_arm7_to_arm9_.front();
            fifo_arm7_to_arm9_.pop();
            triggerInterrupts();
            return value;
        }
    } else {
        // ARM7 reading from ARM9's FIFO
        if (!fifo_arm9_to_arm7_.empty()) {
            uint32_t value = fifo_arm9_to_arm7_.front();
            fifo_arm9_to_arm7_.pop();
            triggerInterrupts();
            return value;
        }
    }
    return 0;
}

uint32_t IPC::readFIFOCtrl(bool is_arm9) const {
    // Control register format:
    // Bit 0: Send FIFO Empty (1 = empty, 0 = not empty)
    // Bit 1: Send FIFO Full (1 = full, 0 = not full)
    // Bit 8: Receive FIFO Empty (1 = empty, 0 = not empty)
    // Bit 9: Receive FIFO Full (1 = full, 0 = not full)
    // Bit 14: FIFO Enable (1 = enabled, 0 = disabled)
    // Bits 2-7, 10-13, 15-31: Reserved or writable control bits
    
    uint32_t ctrl = is_arm9 ? fifo_ctrl_arm9_ : fifo_ctrl_arm7_;
    
    // CRITICAL: Default to enabled and empty if control register is 0
    // This ensures games can use the FIFO immediately
    // Pokemon expects: Bit 0 (Send Empty) = 1, Bit 8 (Receive Empty) = 1, Bit 14 (Enable) = 1
    if (ctrl == 0) {
        ctrl = 0x4101;  // Bit 14 (Enable) = 1, Bit 8 (Receive Empty) = 1, Bit 0 (Send Empty) = 1
    }
    
    // Update status bits (read-only, bits 0-1, 8-9)
    if (is_arm9) {
        // ARM9's view: send = ARM9->ARM7, recv = ARM7->ARM9
        if (fifo_arm9_to_arm7_.empty()) {
            ctrl |= 0x0001;  // Send FIFO empty
        } else {
            ctrl &= ~0x0001;
        }
        if (fifo_arm9_to_arm7_.size() >= 16) {
            ctrl |= 0x0002;  // Send FIFO full
        } else {
            ctrl &= ~0x0002;
        }
        if (fifo_arm7_to_arm9_.empty()) {
            ctrl |= 0x0100;  // Receive FIFO empty
        } else {
            ctrl &= ~0x0100;
        }
        if (fifo_arm7_to_arm9_.size() >= 16) {
            ctrl |= 0x0200;  // Receive FIFO full
        } else {
            ctrl &= ~0x0200;
        }
    } else {
        // ARM7's view: send = ARM7->ARM9, recv = ARM9->ARM7
        if (fifo_arm7_to_arm9_.empty()) {
            ctrl |= 0x0001;  // Send FIFO empty
        } else {
            ctrl &= ~0x0001;
        }
        if (fifo_arm7_to_arm9_.size() >= 16) {
            ctrl |= 0x0002;  // Send FIFO full
        } else {
            ctrl &= ~0x0002;
        }
        if (fifo_arm9_to_arm7_.empty()) {
            ctrl |= 0x0100;  // Receive FIFO empty
        } else {
            ctrl &= ~0x0100;
        }
        if (fifo_arm9_to_arm7_.size() >= 16) {
            ctrl |= 0x0200;  // Receive FIFO full
        } else {
            ctrl &= ~0x0200;
        }
    }
    
    // Ensure FIFO is enabled by default (bit 14)
    // Games expect this to be set for FIFO to work
    ctrl |= 0x4000;  // FIFO Enable bit
    
    // CRITICAL: Ensure default status bits are set correctly
    // Bit 0: Send FIFO Empty = 1 (FIFO is empty, ready to send)
    // Bit 8: Receive FIFO Empty = 1 (FIFO is empty, no data to receive)
    // Bit 14: FIFO Enable = 1 (FIFO is enabled)
    // This gives us 0x4000 | 0x0100 | 0x0001 = 0x4101
    // But we update status bits above, so this is just ensuring enable bit is set
    // The status bits (0, 1, 8, 9) are updated based on FIFO state above
    
    return ctrl;
}

void IPC::writeFIFOSend(bool is_arm9, uint32_t value) {
    // Writing to FIFO Send pushes a value into the FIFO
    if (is_arm9) {
        // ARM9 sending to ARM7
        if (fifo_arm9_to_arm7_.size() < 16) {
            fifo_arm9_to_arm7_.push(value);
            updateFIFOStatus();
            triggerInterrupts();
        } else {
            // FIFO full - ignore or handle error
            std::fprintf(stderr, "IPC: ARM9->ARM7 FIFO full, dropping value 0x%08X\n", value);
        }
    } else {
        // ARM7 sending to ARM9
        if (fifo_arm7_to_arm9_.size() < 16) {
            fifo_arm7_to_arm9_.push(value);
            updateFIFOStatus();
            triggerInterrupts();
        } else {
            // FIFO full - ignore or handle error
            std::fprintf(stderr, "IPC: ARM7->ARM9 FIFO full, dropping value 0x%08X\n", value);
        }
    }
}

void IPC::writeFIFORecv(bool is_arm9, uint32_t value) {
    // Writing to FIFO Receive is used to acknowledge/clear the receive FIFO
    // In practice, reading from FIFO Receive already pops the value
    // Writing here might be used for synchronization
    // For now, we'll just update status
    updateFIFOStatus();
}

void IPC::writeFIFOCtrl(bool is_arm9, uint32_t value) {
    // Control register: bits 0-1 are read-only status, bits 8-15 are writable
    // Bit 3 = Clear Send FIFO (when set, empty the send FIFO)
    // Bit 11 = Clear Receive FIFO (when set, empty the receive FIFO)
    
    // Handle FIFO clear bits
    if (value & 0x0008) {  // Bit 3: Clear Send FIFO
        if (is_arm9) {
            // Clear ARM9->ARM7 FIFO (send FIFO for ARM9)
            while (!fifo_arm9_to_arm7_.empty()) {
                fifo_arm9_to_arm7_.pop();
            }
        } else {
            // Clear ARM7->ARM9 FIFO (send FIFO for ARM7)
            while (!fifo_arm7_to_arm9_.empty()) {
                fifo_arm7_to_arm9_.pop();
            }
        }
    }
    
    if (value & 0x0800) {  // Bit 11: Clear Receive FIFO
        if (is_arm9) {
            // Clear ARM7->ARM9 FIFO (receive FIFO for ARM9)
            while (!fifo_arm7_to_arm9_.empty()) {
                fifo_arm7_to_arm9_.pop();
            }
        } else {
            // Clear ARM9->ARM7 FIFO (receive FIFO for ARM7)
            while (!fifo_arm9_to_arm7_.empty()) {
                fifo_arm9_to_arm7_.pop();
            }
        }
    }
    
    // Update control register (preserve status bits 0-1, 8-9)
    if (is_arm9) {
        fifo_ctrl_arm9_ = (fifo_ctrl_arm9_ & 0x00FF) | (value & 0xFF00);
    } else {
        fifo_ctrl_arm7_ = (fifo_ctrl_arm7_ & 0x00FF) | (value & 0xFF00);
    }
    updateFIFOStatus();
    triggerInterrupts();
}

void IPC::updateFIFOStatus() {
    // Update status bits in control registers based on FIFO state
    // This is done automatically when reading/writing
}

void IPC::triggerInterrupts() {
    // Trigger IPC interrupts when FIFO state changes
    if (arm9_interrupts_) {
        // Trigger interrupt if ARM7 sent data to ARM9
        if (!fifo_arm7_to_arm9_.empty()) {
            arm9_interrupts_->requestInterrupt(InterruptController::INT_IPC_RECV_FIFO_NOT_EMPTY);
        }
        // Trigger interrupt if ARM9's send FIFO becomes empty
        if (fifo_arm9_to_arm7_.empty()) {
            arm9_interrupts_->requestInterrupt(InterruptController::INT_IPC_SEND_FIFO_EMPTY);
        }
    }
    
    if (arm7_interrupts_) {
        // Trigger interrupt if ARM9 sent data to ARM7
        if (!fifo_arm9_to_arm7_.empty()) {
            arm7_interrupts_->requestInterrupt(InterruptController::INT_IPC_RECV_FIFO_NOT_EMPTY);
        }
        // Trigger interrupt if ARM7's send FIFO becomes empty
        if (fifo_arm7_to_arm9_.empty()) {
            arm7_interrupts_->requestInterrupt(InterruptController::INT_IPC_SEND_FIFO_EMPTY);
        }
    }
}

uint32_t IPC::readIPCSYNC(bool is_arm9) const {
    // IPCSYNC register format:
    // Bits 0-3: ARM9 sync bits (ARM9 writes, ARM7 reads)
    // Bits 8-11: ARM7 sync bits (ARM7 writes, ARM9 reads)
    // Bits 4-7, 12-31: Reserved/read-only
    
    if (is_arm9) {
        // ARM9 reads ARM7's sync bits (bits 8-11)
        return (ipcsync_ >> 8) & 0xF;
    } else {
        // ARM7 reads ARM9's sync bits (bits 0-3)
        return ipcsync_ & 0xF;
    }
}

void IPC::writeIPCSYNC(bool is_arm9, uint32_t value) {
    // IPCSYNC register: each CPU writes to its own bits
    uint32_t old_value = ipcsync_;
    if (is_arm9) {
        // ARM9 writes to bits 0-3
        ipcsync_ = (ipcsync_ & 0xFFFFFFF0) | (value & 0xF);
        // Log handshake for debugging
        if ((ipcsync_ & 0xF) != (old_value & 0xF)) {
            std::printf("IPC: ARM9 wrote to IPCSYNC = 0x%X (bits 0-3)\n", value & 0xF);
        }
        
        // CRITICAL: When ARM9 writes to IPCSYNC, check if bit 13 (Send IRQ) is set
        // If so, trigger IPC Sync interrupt on ARM7
        if ((value & (1U << 13)) != 0 && arm7_interrupts_) {
            arm7_interrupts_->requestInterrupt(InterruptController::INT_IPC_SYNC);
            std::printf("IPC: ARM9 triggered IPC Sync interrupt on ARM7\n");
        }
    } else {
        // ARM7 writes to bits 8-11
        ipcsync_ = (ipcsync_ & 0xFFFFF0FF) | ((value & 0xF) << 8);
        // Log handshake for debugging
        if ((ipcsync_ & 0xF00) != (old_value & 0xF00)) {
            std::printf("IPC: ARM7 wrote to IPCSYNC = 0x%X (bits 8-11)\n", value & 0xF);
        }
    }
    
    // Trigger interrupts when sync bits change
    triggerInterrupts();
}
