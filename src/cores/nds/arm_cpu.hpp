#pragma once

#include <cstdint>
#include <cstdbool>

// Forward declaration
class NDSMemory;

/**
 * ARM CPU core (ARM7TDMI / ARM946E-S compatible)
 * 
 * This is a basic interpreter implementation.
 * Supports ARM and Thumb instruction sets.
 */
class ARMCpu {
public:
    ARMCpu(NDSMemory* memory, bool is_arm9);
    ~ARMCpu();
    
    /**
     * Reset the CPU to initial state.
     */
    void reset();
    
    /**
     * Set the entry point (PC) for execution.
     */
    void setEntryPoint(uint32_t entry);
    
    /**
     * Run the CPU for a specified number of cycles.
     * 
     * @param cycles Number of cycles to execute
     */
    void runCycles(uint32_t cycles);
    
    /**
     * Execute a single instruction.
     */
    void step();
    
    /**
     * Get the current Program Counter.
     */
    uint32_t getPC() const { return registers_[15]; }
    
    /**
     * Get a register value.
     */
    uint32_t getRegister(int reg) const { return registers_[reg]; }
    
    /**
     * Set a register value.
     */
    void setRegister(int reg, uint32_t value);
    
    /**
     * Check if CPU is in Thumb mode.
     */
    bool isThumbMode() const { return cpsr & (1 << 5); }
    
    /**
     * Set BIOS HLE handler (called from NDS core).
     */
    void setBIOSHLE(class BIOSHLE* bios_hle) { bios_hle_ = bios_hle; }
    
    /**
     * Set interrupt controller (called from NDS core).
     */
    void setInterruptController(class InterruptController* interrupts) { interrupts_ = interrupts; }
    
    /**
     * Set memory system (for TCM configuration).
     */
    void setMemory(class NDSMemory* memory) { memory_ = memory; }
    
    /**
     * Check and handle pending interrupts.
     */
    void checkInterrupts();
    
    /**
     * Check if CPU is halted (waiting for interrupt).
     */
    bool isHalted() const { return halted_; }
    
    /**
     * Set halted state (called by BIOS HLE).
     */
    void setHalted(bool halted) { halted_ = halted; }
    
    /**
     * Get CPSR value.
     */
    uint32_t getCPSR() const { return cpsr; }
    
    /**
     * Set CPSR value.
     */
    void setCPSR(uint32_t value) { cpsr = value; }

private:
    NDSMemory* memory_;
    bool is_arm9_;
    
    // ARM registers (R0-R15)
    uint32_t registers_[16];
    
    // Current Program Status Register
    uint32_t cpsr;
    
    // Saved Program Status Registers (for different modes)
    uint32_t spsr;
    
    // Cycle counter
    uint32_t cycles_;
    
    // Halted flag (CPU waiting for interrupt)
    bool halted_;
    
    // BIOS HLE handler
    class BIOSHLE* bios_hle_;
    
    // Interrupt controller
    class InterruptController* interrupts_;
    
    /**
     * Execute an ARM instruction.
     */
    void executeARM(uint32_t instruction);
    
    /**
     * Execute a Thumb instruction.
     */
    void executeThumb(uint16_t instruction);
    
    /**
     * Read from memory (handles unaligned access).
     */
    uint32_t readMemory(uint32_t address, int size);
    
    /**
     * Write to memory (handles unaligned access).
     */
    void writeMemory(uint32_t address, uint32_t value, int size);
    
    /**
     * Check condition codes.
     */
    bool checkCondition(uint32_t instruction);
    
    /**
     * Update CPSR flags after arithmetic operation.
     */
    void updateFlags(uint32_t result, uint32_t op1, uint32_t op2, bool is_subtract);
    
    /**
     * Handle data processing instruction.
     */
    void dataProcessing(uint32_t instruction);
    
    /**
     * Handle branch instruction.
     */
    void branch(uint32_t instruction);
    
    /**
     * Handle load/store instruction.
     */
    void loadStore(uint32_t instruction);
    
    /**
     * Handle halfword/byte load/store instruction (LDRH, STRH, LDRSB, LDRSH).
     */
    void loadStoreHalfword(uint32_t instruction);
    
    /**
     * Handle software interrupt (SWI).
     */
    void softwareInterrupt(uint32_t instruction);
    
    /**
     * Handle CP15 (System Control Coprocessor) instructions.
     */
    void handleCP15(uint32_t instruction);
};
