#pragma once

#include <cstdint>

// Forward declaration
class InterruptController;

/**
 * NDS Timer System
 * 
 * Each CPU (ARM9 and ARM7) has 4 timers.
 * Timers can count up or down, generate interrupts, and cascade.
 */
class TimerSystem {
public:
    TimerSystem(InterruptController* interrupts, bool is_arm9);
    ~TimerSystem();
    
    /**
     * Reset all timers.
     */
    void reset();
    
    /**
     * Advance timers by the given number of cycles.
     */
    void step(uint32_t cycles);
    
    /**
     * Read timer control register (16-bit).
     */
    uint16_t readTMCNT(int timer_num);
    
    /**
     * Write timer control register (16-bit).
     */
    void writeTMCNT(int timer_num, uint16_t value);
    
    /**
     * Read timer counter register (16-bit).
     */
    uint16_t readTMCNT_L(int timer_num);
    
    /**
     * Write timer counter register (16-bit).
     */
    void writeTMCNT_L(int timer_num, uint16_t value);

private:
    InterruptController* interrupts_;
    bool is_arm9_;
    
    struct Timer {
        uint16_t counter;      // Current counter value
        uint16_t reload;        // Reload value
        uint16_t control;       // Control register
        bool enabled;           // Timer enabled
        bool cascade;           // Cascade from previous timer
        int prescaler;          // Prescaler (0=1, 1=64, 2=256, 3=1024)
        int prescaler_counter;  // Prescaler counter
        bool irq_enabled;       // Interrupt enabled
    };
    
    Timer timers_[4];
    
    /**
     * Update a timer.
     */
    void updateTimer(int timer_num, uint32_t cycles);
    
    /**
     * Get prescaler divisor.
     */
    int getPrescalerDivisor(int prescaler) const;
};
