#pragma once

#include <cstdint>

// Forward declarations
class NDSMemory;
class InterruptController;

/**
 * NDS DMA Controller
 * 
 * Handles Direct Memory Access transfers for both ARM9 and ARM7.
 * Each CPU has 4 DMA channels.
 */
class DMA {
public:
    DMA();
    ~DMA();

    void setMemory(NDSMemory* memory);
    void setInterruptController(InterruptController* interrupts, bool is_arm9);

    // Register access
    uint32_t readDMA_SAD(uint32_t channel, bool is_arm9);
    uint32_t readDMA_DAD(uint32_t channel, bool is_arm9);
    uint32_t readDMA_CNT(uint32_t channel, bool is_arm9);
    
    void writeDMA_SAD(uint32_t channel, bool is_arm9, uint32_t value);
    void writeDMA_DAD(uint32_t channel, bool is_arm9, uint32_t value);
    void writeDMA_CNT(uint32_t channel, bool is_arm9, uint32_t value);

    /**
     * Process pending DMA transfers.
     * Should be called periodically during emulation.
     */
    void processTransfers();

private:
    struct DMAChannel {
        uint32_t source_addr;
        uint32_t dest_addr;
        uint32_t control;
        bool active;
        uint32_t remaining_count;
    };

    NDSMemory* memory_;
    InterruptController* arm9_interrupts_;
    InterruptController* arm7_interrupts_;

    DMAChannel arm9_channels_[4];
    DMAChannel arm7_channels_[4];

    void executeTransfer(DMAChannel& channel, bool is_arm9);
    void triggerInterrupt(uint32_t channel, bool is_arm9);
};
