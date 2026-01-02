#pragma once

#include <cstdint>

// Forward declaration
class NDSMemory;

/**
 * NDS I/O Register Handler
 * 
 * Handles reads/writes to I/O registers (0x04000000+).
 * This includes graphics, audio, input, timers, interrupts, etc.
 */
class IORegisters {
public:
    IORegisters(NDSMemory* memory);
    ~IORegisters();
    
    /**
     * Read from I/O register.
     */
    uint32_t read32(uint32_t address);
    uint16_t read16(uint32_t address);
    uint8_t read8(uint32_t address);
    
    /**
     * Write to I/O register.
     */
    void write32(uint32_t address, uint32_t value);
    void write16(uint32_t address, uint16_t value);
    void write8(uint32_t address, uint8_t value);
    
    /**
     * Get display control register for main screen.
     */
    uint16_t getMainDisplayControl() const { return main_display_control_; }
    
    /**
     * Get display control register for sub screen.
     */
    uint16_t getSubDisplayControl() const { return sub_display_control_; }

private:
    NDSMemory* memory_;
    
    // Display control registers
    uint16_t main_display_control_;  // 0x04000000
    uint16_t sub_display_control_;    // 0x04001000
    
    // Background control registers (simplified - NDS has 4 backgrounds per screen)
    uint16_t bg_control_[8];  // 4 for main, 4 for sub
    
    // Input registers
    uint16_t key_input_;  // 0x04000130
    
    /**
     * Handle register-specific behavior.
     */
    void handleRegisterWrite(uint32_t address, uint32_t value);
};
