#pragma once

#include <cstdint>
#include <queue>

// Forward declarations
class InterruptController;

/**
 * IPC (Inter-Processor Communication) System
 * 
 * Handles FIFO communication between ARM9 and ARM7.
 * Registers:
 * - 0x04000180: FIFO Send (write-only for sender, read-only for receiver)
 * - 0x04000184: FIFO Receive (read-only for receiver, write-only for sender)
 * - 0x04000188: FIFO Control (read/write for both)
 */
class IPC {
public:
    IPC();
    ~IPC();

    void setInterruptController(InterruptController* interrupts, bool is_arm9);

    // Register access
    uint32_t readFIFOSend(bool is_arm9) const;
    uint32_t readFIFORecv(bool is_arm9);  // Not const - pops from queue
    uint32_t readFIFOCtrl(bool is_arm9) const;

    void writeFIFOSend(bool is_arm9, uint32_t value);
    void writeFIFORecv(bool is_arm9, uint32_t value);
    void writeFIFOCtrl(bool is_arm9, uint32_t value);

    // IPCSYNC register (0x04000180 for ARM9, 0x04100000 for ARM7)
    uint32_t readIPCSYNC(bool is_arm9) const;
    void writeIPCSYNC(bool is_arm9, uint32_t value);

private:
    // FIFO queues (16 entries each, 32-bit values)
    std::queue<uint32_t> fifo_arm9_to_arm7_;  // ARM9 sends to ARM7
    std::queue<uint32_t> fifo_arm7_to_arm9_;  // ARM7 sends to ARM9

    // Control registers
    uint16_t fifo_ctrl_arm9_;  // ARM9's view of control register
    uint16_t fifo_ctrl_arm7_;  // ARM7's view of control register
    
    // IPCSYNC register (shared between ARM9 and ARM7)
    // ARM9 writes to bits 0-3, ARM7 reads from bits 8-11
    // ARM7 writes to bits 8-11, ARM9 reads from bits 0-3
    uint32_t ipcsync_;

    // Interrupt controllers
    InterruptController* arm9_interrupts_;
    InterruptController* arm7_interrupts_;

    // Helper functions
    void updateFIFOStatus();
    void triggerInterrupts();
};
