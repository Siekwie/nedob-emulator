#pragma once

#include <cstdint>

// Forward declarations
class NDSMemory;
class ARMCpu;
class InterruptController;

/**
 * BIOS High-Level Emulation (HLE)
 * 
 * Implements NDS BIOS functions via SWI calls.
 * This allows games to boot without requiring actual BIOS files.
 */
class BIOSHLE {
public:
    BIOSHLE(NDSMemory* memory);
    ~BIOSHLE();
    
    /**
     * Handle a software interrupt (SWI) call.
     * 
     * @param cpu The CPU that made the SWI call
     * @param swi_number The SWI function number
     * @return true if the SWI was handled, false otherwise
     */
    bool handleSWI(ARMCpu* cpu, uint32_t swi_number);
    
    /**
     * Check if a SWI number is implemented.
     */
    bool isSWIImplemented(uint32_t swi_number) const;

    /**
     * Set interrupt controllers for VBlankIntrWait.
     */
    void setInterruptController(InterruptController* interrupts, bool is_arm9);

    /**
     * Set core reference for SoftReset.
     */
    void setCore(class NDSCore* core);

private:
    NDSMemory* memory_;
    InterruptController* arm9_interrupts_;
    InterruptController* arm7_interrupts_;
    class NDSCore* core_;  // For SoftReset
    
    // ARM9 BIOS functions
    bool handleSWI_ARM9(ARMCpu* cpu, uint32_t swi_number);
    
    // ARM7 BIOS functions
    bool handleSWI_ARM7(ARMCpu* cpu, uint32_t swi_number);
    
    // Common BIOS functions
    void swi_SoftReset(ARMCpu* cpu);
    void swi_RegisterRamReset(ARMCpu* cpu);
    void swi_Halt(ARMCpu* cpu);
    void swi_Stop(ARMCpu* cpu);
    void swi_IntrWait(ARMCpu* cpu);
    void swi_VBlankIntrWait(ARMCpu* cpu);
    void swi_Div(ARMCpu* cpu);
    void swi_DivArm(ARMCpu* cpu);
    void swi_Sqrt(ARMCpu* cpu);
    void swi_ArcTan(ARMCpu* cpu);
    void swi_ArcTan2(ARMCpu* cpu);
    void swi_CpuSet(ARMCpu* cpu);
    void swi_CpuFastSet(ARMCpu* cpu);
    void swi_GetBiosChecksum(ARMCpu* cpu);
    void swi_BgAffineSet(ARMCpu* cpu);
    void swi_ObjAffineSet(ARMCpu* cpu);
    void swi_BitUnPack(ARMCpu* cpu);
    void swi_LZ77UnCompReadNormalWrite8bit(ARMCpu* cpu);
    void swi_LZ77UnCompReadNormalWrite16bit(ARMCpu* cpu);
    void swi_HuffUnCompReadNormal(ARMCpu* cpu);
    void swi_RLUnCompReadNormalWrite8bit(ARMCpu* cpu);
    void swi_RLUnCompReadNormalWrite16bit(ARMCpu* cpu);
    void swi_Diff8bitUnFilterWrite8bit(ARMCpu* cpu);
    void swi_Diff8bitUnFilterWrite16bit(ARMCpu* cpu);
    void swi_Diff16bitUnFilter(ARMCpu* cpu);
};
