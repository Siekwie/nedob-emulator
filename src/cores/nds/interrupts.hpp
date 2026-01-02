#pragma once

#include <cstdint>
#include <cstdio>

/**
 * NDS Interrupt Controller
 * 
 * Handles interrupt enable/disable, flags, and priority.
 * Both ARM9 and ARM7 have their own interrupt controllers.
 */
class InterruptController {
public:
    InterruptController();
    ~InterruptController();
    
    /**
     * Interrupt types (bits in interrupt flags)
     */
    enum InterruptType {
        INT_LCD_VBLANK = 0,
        INT_LCD_HBLANK = 1,
        INT_LCD_VCOUNT = 2,
        INT_TIMER0 = 3,
        INT_TIMER1 = 4,
        INT_TIMER2 = 5,
        INT_TIMER3 = 6,
        INT_DMA0 = 8,
        INT_DMA1 = 9,
        INT_DMA2 = 10,
        INT_DMA3 = 11,
        INT_KEYPAD = 12,
        INT_GAMEPAK = 13,
        INT_IPC_SYNC = 16,
        INT_IPC_SEND_FIFO_EMPTY = 17,
        INT_IPC_RECV_FIFO_NOT_EMPTY = 18,
        INT_COUNT = 32  // Total number of interrupt types
    };
    
    /**
     * Read interrupt enable register.
     */
    uint32_t readIE() const { return interrupt_enable_; }
    
    /**
     * Write interrupt enable register.
     */
    void writeIE(uint32_t value) {
        static bool logged = false;
        if (!logged && value != 0) {
            std::printf("InterruptController: IE register set to 0x%08X\n", value);
            logged = true;
        }
        interrupt_enable_ = value;
    }
    
    /**
     * Read interrupt flag register.
     */
    uint32_t readIF() const { return interrupt_flags_; }
    
    /**
     * Write interrupt flag register (write 1 to clear).
     */
    void writeIF(uint32_t value) { interrupt_flags_ &= ~value; }
    
    /**
     * Request an interrupt.
     */
    void requestInterrupt(InterruptType type);
    
    /**
     * Check if any enabled interrupt is pending.
     */
    bool hasPendingInterrupt() const;
    
    /**
     * Check if a specific interrupt type is pending.
     */
    bool hasPendingInterrupt(InterruptType type) const;
    
    /**
     * Get the highest priority pending interrupt.
     */
    InterruptType getPendingInterrupt() const;
    
    /**
     * Clear an interrupt flag.
     */
    void clearInterrupt(InterruptType type);

private:
    uint32_t interrupt_enable_;   // Interrupt Enable Register
    uint32_t interrupt_flags_;    // Interrupt Flag Register (write 1 to clear)
};
