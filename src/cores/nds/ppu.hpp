#pragma once

#include <cstdint>
#include <vector>
#include <memory>

// Forward declarations
class NDSMemory;

/**
 * NDS PPU (Picture Processing Unit) - Graphics Engine
 * 
 * Handles rendering for both main and sub screens.
 * For now, implements basic software rendering.
 */
class PPU {
public:
    PPU(NDSMemory* memory);
    ~PPU();
    
    /**
     * Initialize the PPU.
     */
    void initialize();
    
    /**
     * Render one frame.
     * This reads VRAM and generates the frame buffer.
     */
    void renderFrame();
    
    /**
     * Get the main screen frame buffer (256x192, BGR555 format).
     */
    const uint16_t* getMainScreenBuffer() const { return main_screen_.data(); }
    
    /**
     * Get the sub screen frame buffer (256x192, BGR555 format).
     */
    const uint16_t* getSubScreenBuffer() const { return sub_screen_.data(); }
    
    /**
     * Get screen dimensions.
     */
    static constexpr int SCREEN_WIDTH = 256;
    static constexpr int SCREEN_HEIGHT = 192;
    
    /**
     * Set interrupt controllers for VBlank generation.
     */
    void setInterruptController(class InterruptController* interrupts, bool is_arm9);

    /**
     * Read DISPSTAT register (0x04000004 for main, 0x04001004 for sub).
     */
    uint16_t readDISPSTAT(bool is_main) const;

    /**
     * Read VCOUNT register (0x04000006 for main, 0x04001006 for sub).
     */
    uint16_t readVCOUNT(bool is_main) const;

    /**
     * Update scanline number (called during frame execution).
     */
    void updateScanline(uint32_t scanline);

private:
    NDSMemory* memory_;
    class InterruptController* arm9_interrupts_;
    class InterruptController* arm7_interrupts_;
    
    // Frame buffers (BGR555 format, 256x192 = 49152 pixels)
    std::vector<uint16_t> main_screen_;
    std::vector<uint16_t> sub_screen_;
    
    // Display control registers (simplified)
    struct DisplayControl {
        uint16_t main_display_control;
        uint16_t sub_display_control;
    } display_control_;
    
    // Scanline and frame tracking
    int scanline_;           // Current scanline (0-261)
    int frame_count_;         // Frame counter
    uint32_t scanline_cycles_; // Cycles elapsed in current scanline
    uint16_t dispstat_main_;  // DISPSTAT register for main screen
    uint16_t dispstat_sub_;   // DISPSTAT register for sub screen
    uint8_t vcount_match_main_; // VCOUNT match value for main screen
    uint8_t vcount_match_sub_;  // VCOUNT match value for sub screen
    
    /**
     * Read display control registers from memory.
     */
    void readDisplayControl();
    
    /**
     * Render a single screen (main or sub).
     */
    void renderScreen(bool is_main);
    
    /**
     * Convert BGR555 to RGB888.
     */
    void convertBGR555ToRGB888(const uint16_t* src, uint8_t* dst, int width, int height);
    
    /**
     * Render a simple test pattern (for debugging).
     */
    void renderTestPattern();
    
    /**
     * Render a text mode background.
     */
    void renderTextBackground(uint16_t* screen, bool is_main, int bg_num);
};
