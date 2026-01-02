#include "ppu.hpp"
#include "memory.hpp"
#include "interrupts.hpp"
#include <cstdio>
#include <cstring>

// NDS I/O Register Addresses
namespace {
    constexpr uint32_t REG_DISPCNT_MAIN = 0x04000000;
    constexpr uint32_t REG_DISPCNT_SUB = 0x04001000;
    constexpr uint32_t REG_VRAMCNT_A = 0x04000240;
}

PPU::PPU(NDSMemory* memory)
    : memory_(memory)
    , arm9_interrupts_(nullptr)
    , arm7_interrupts_(nullptr)
    , scanline_(0)
    , frame_count_(0)
    , scanline_cycles_(0)
    , dispstat_main_(0)
    , dispstat_sub_(0)
    , vcount_match_main_(0)
    , vcount_match_sub_(0)
{
    main_screen_.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0);
    sub_screen_.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0);
    std::memset(&display_control_, 0, sizeof(display_control_));
    // Initialize scanline to a visible line (not VBlank)
    scanline_ = 0;
}

PPU::~PPU() {
}

void PPU::initialize() {
    // Initialize frame buffers to black
    std::fill(main_screen_.begin(), main_screen_.end(), 0);
    std::fill(sub_screen_.begin(), sub_screen_.end(), 0);
    
    std::printf("PPU initialized\n");
}

void PPU::renderFrame() {
    // Read display control registers
    readDisplayControl();
    
    // Update scanline timing
    // NDS runs at ~16.78MHz pixel clock, 262 scanlines per frame
    // Each scanline is ~355 cycles at 67MHz (ARM9 clock)
    // For simplicity, we'll update scanline based on frame rendering
    // Increment scanline each frame to simulate timing
    scanline_++;
    if (scanline_ >= 262) {
        scanline_ = 0;  // Wrap around
    }
    
    // Update DISPSTAT based on current scanline
    if (scanline_ >= 192) {
        // VBlank period (scanlines 192-261)
        dispstat_main_ |= 0x01;  // Set VBlank flag
        dispstat_sub_ |= 0x01;
    } else {
        // Visible period (scanlines 0-191)
        dispstat_main_ &= ~0x01;  // Clear VBlank flag
        dispstat_sub_ &= ~0x01;
    }
    
    // Debug: Log display control every 60 frames (once per second at 60 FPS)
    frame_count_++;
    if (frame_count_ == 1 || frame_count_ % 60 == 0) {
        std::printf("PPU: Frame %d - Main DISPCNT=0x%04X, Sub DISPCNT=0x%04X\n", 
                    frame_count_,
                    display_control_.main_display_control, 
                    display_control_.sub_display_control);
        std::fflush(stdout);  // Force output
    }
    
    // Always try to render actual game graphics
    // Even if display is disabled, the game might be setting up graphics
    renderScreen(true);   // Main screen
    renderScreen(false);  // Sub screen
    
    // NOTE: VBlank interrupt is now triggered in updateScanline() when scanline crosses 192
    // This ensures interrupts fire during frame execution, not after
}

void PPU::setInterruptController(InterruptController* interrupts, bool is_arm9) {
    if (is_arm9) {
        arm9_interrupts_ = interrupts;
    } else {
        arm7_interrupts_ = interrupts;
    }
}

void PPU::readDisplayControl() {
    // Read display control registers from I/O space
    display_control_.main_display_control = memory_->read16_ARM9(REG_DISPCNT_MAIN);
    display_control_.sub_display_control = memory_->read16_ARM9(REG_DISPCNT_SUB);
}

void PPU::renderScreen(bool is_main) {
    uint16_t* screen = is_main ? main_screen_.data() : sub_screen_.data();
    uint16_t dispcnt = is_main ? display_control_.main_display_control : display_control_.sub_display_control;
    
    // Get background mode (bits 0-2)
    int bg_mode = dispcnt & 0x7;
    
    // Check which backgrounds are enabled (bits 8-11)
    bool bg0_enabled = (dispcnt & (1 << 8)) != 0;
    bool bg1_enabled = (dispcnt & (1 << 9)) != 0;
    bool bg2_enabled = (dispcnt & (1 << 10)) != 0;
    bool bg3_enabled = (dispcnt & (1 << 11)) != 0;
    
    // Check if display is enabled (bit 7)
    bool display_enabled = (dispcnt & 0x80) != 0;
    
    // Debug logging every 60 frames
    static int debug_frame = 0;
    debug_frame++;
    if ((debug_frame == 1 || debug_frame % 60 == 0) && is_main) {
        std::printf("PPU: Main screen - Mode=%d, BG0=%d BG1=%d BG2=%d BG3=%d, Display=%d, DISPCNT=0x%04X\n",
                    bg_mode, bg0_enabled, bg1_enabled, bg2_enabled, bg3_enabled, display_enabled, dispcnt);
        std::fflush(stdout);  // Force output
    }
    
    // For now, implement Mode 0 (text mode) - simplest background mode
    // Mode 0 supports 4 text backgrounds
    if (bg_mode == 0) {
        bool rendered = false;
        // Try to render enabled backgrounds (BG0 has highest priority)
        if (bg0_enabled) {
            renderTextBackground(screen, is_main, 0);
            rendered = true;
        } else if (bg1_enabled) {
            renderTextBackground(screen, is_main, 1);
            rendered = true;
        } else if (bg2_enabled) {
            renderTextBackground(screen, is_main, 2);
            rendered = true;
        } else if (bg3_enabled) {
            renderTextBackground(screen, is_main, 3);
            rendered = true;
        }
        
        if (!rendered) {
            // No backgrounds enabled - check if we should try to render anyway
            // Some games might have graphics in VRAM even if backgrounds aren't enabled yet
            // Try BG0 anyway to see if there's any data
            if (is_main) {
                // Try to render BG0 even if not enabled, to see if there's data
                uint32_t bgcnt_addr = 0x04000008;
                uint16_t bgcnt = memory_->read16_ARM9(bgcnt_addr);
                if (bgcnt != 0) {
                    // Background is configured, try rendering it
                    renderTextBackground(screen, is_main, 0);
                    rendered = true;
                }
            }
            
            if (!rendered) {
                // No backgrounds enabled and no data, fill with black
                std::fill(screen, screen + (SCREEN_WIDTH * SCREEN_HEIGHT), 0);
            }
        }
    } else {
        // Other modes not yet implemented - fill with black for now
        std::fill(screen, screen + (SCREEN_WIDTH * SCREEN_HEIGHT), 0);
    }
}

void PPU::renderTextBackground(uint16_t* screen, bool is_main, int bg_num) {
    // Text mode background rendering
    // Each background has:
    // - Tile map base (in VRAM)
    // - Tile data base (in VRAM)
    // - Palette (in palette RAM)
    // - Scroll registers (X, Y)
    
    // Read background control register
    uint32_t bgcnt_addr = 0x04000008 + (bg_num * 2);
    if (!is_main) {
        bgcnt_addr = 0x04001008 + (bg_num * 2);
    }
    uint16_t bgcnt = memory_->read16_ARM9(bgcnt_addr);
    
    // If background control is 0, background is not configured yet
    if (bgcnt == 0) {
        // Fill with a debug color to see if rendering is being attempted
        static bool logged = false;
        if (!logged && is_main && bg_num == 0) {
            std::printf("PPU: BG%d control is 0, not rendering\n", bg_num);
            logged = true;
        }
        return;
    }
    
    // Debug: Log background configuration once
    static bool bg_logged[8] = {false};
    int bg_index = (is_main ? 0 : 4) + bg_num;
    if (!bg_logged[bg_index]) {
        std::printf("PPU: Rendering %s BG%d: BGCNT=0x%04X\n", 
                    is_main ? "Main" : "Sub", bg_num, bgcnt);
        bg_logged[bg_index] = true;
    }
    
    // Extract background parameters
    int tile_map_base = (bgcnt & 0x1F) * 0x800;      // Bits 0-4: tile map base (2KB blocks)
    int tile_data_base = ((bgcnt >> 2) & 0xF) * 0x4000; // Bits 2-5: tile data base (16KB blocks)
    bool use_256_colors = (bgcnt & 0x80) != 0;       // Bit 7: 256 color mode
    
    // Read scroll registers
    uint32_t bgx_addr = 0x04000010 + (bg_num * 4);
    uint32_t bgy_addr = 0x04000012 + (bg_num * 4);
    if (!is_main) {
        bgx_addr = 0x04001010 + (bg_num * 4);
        bgy_addr = 0x04001012 + (bg_num * 4);
    }
    int16_t scroll_x = static_cast<int16_t>(memory_->read16_ARM9(bgx_addr));
    int16_t scroll_y = static_cast<int16_t>(memory_->read16_ARM9(bgy_addr));
    
    // Tile map is 32x32 or 64x64 tiles (depending on bgcnt bit 14)
    bool large_map = (bgcnt & 0x4000) != 0;
    int map_width = large_map ? 64 : 32;
    int map_height = large_map ? 64 : 32;
    
    // Render the background
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            // Calculate tile coordinates
            int tile_x = (x + scroll_x) / 8;
            int tile_y = (y + scroll_y) / 8;
            
            // Wrap coordinates
            tile_x = tile_x % map_width;
            tile_y = tile_y % map_height;
            if (tile_x < 0) tile_x += map_width;
            if (tile_y < 0) tile_y += map_height;
            
            // Read tile map entry (2 bytes per tile)
            uint32_t tile_map_addr = 0x06000000 + tile_map_base + (tile_y * map_width + tile_x) * 2;
            uint16_t tile_entry = memory_->read16_ARM9(tile_map_addr);
            
            // Extract tile information
            int tile_num = tile_entry & 0x3FF;        // Bits 0-9: tile number
            bool h_flip = (tile_entry & 0x400) != 0; // Bit 10: horizontal flip
            bool v_flip = (tile_entry & 0x800) != 0; // Bit 11: vertical flip
            int palette_num = (tile_entry >> 12) & 0xF; // Bits 12-15: palette (for 16-color mode)
            
            // Calculate pixel within tile
            int px_in_tile = (x + scroll_x) % 8;
            int py_in_tile = (y + scroll_y) % 8;
            if (px_in_tile < 0) px_in_tile += 8;
            if (py_in_tile < 0) py_in_tile += 8;
            
            if (h_flip) px_in_tile = 7 - px_in_tile;
            if (v_flip) py_in_tile = 7 - py_in_tile;
            
            // Read tile data
            uint32_t tile_data_addr = 0x06000000 + tile_data_base + (tile_num * (use_256_colors ? 64 : 32)) + (py_in_tile * (use_256_colors ? 8 : 4));
            
            uint16_t pixel_color = 0;
            if (use_256_colors) {
                // 256-color mode: 1 byte per pixel
                uint8_t color_index = memory_->read8_ARM9(tile_data_addr + px_in_tile);
                if (color_index != 0) {  // 0 is transparent
                    // Read from palette (256 colors = 512 bytes = 0x200)
                    // Main screen uses 0x05000000-0x05000200, sub uses 0x05000400-0x05000600
                    uint32_t palette_base = is_main ? 0x05000000 : 0x05000400;
                    uint32_t palette_addr = palette_base + (color_index * 2);
                    pixel_color = memory_->read16_ARM9(palette_addr);
                }
            } else {
                // 16-color mode: 4 bits per pixel (2 pixels per byte)
                uint8_t tile_byte = memory_->read8_ARM9(tile_data_addr + (px_in_tile / 2));
                uint8_t color_index;
                if (px_in_tile & 1) {
                    color_index = (tile_byte >> 4) & 0xF;
                } else {
                    color_index = tile_byte & 0xF;
                }
                
                if (color_index != 0) {  // 0 is transparent
                    // Read from palette (16 colors per palette = 32 bytes)
                    // Main screen palettes: 0x05000000-0x05000200 (16 palettes)
                    // Sub screen palettes: 0x05000400-0x05000600 (16 palettes)
                    uint32_t palette_base = is_main ? 0x05000000 : 0x05000400;
                    uint32_t palette_addr = palette_base + (palette_num * 0x20) + (color_index * 2);
                    pixel_color = memory_->read16_ARM9(palette_addr);
                }
            }
            
            screen[y * SCREEN_WIDTH + x] = pixel_color;
        }
    }
}

void PPU::renderTestPattern() {
    // Render a simple test pattern to verify rendering works
    // This will help us see if the PPU is being called
    
    static int frame_count = 0;
    frame_count++;
    
    // Main screen: animated gradient pattern
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            // Create an animated gradient pattern
            uint8_t r = ((x + frame_count) * 31) / SCREEN_WIDTH % 32;
            uint8_t g = ((y + frame_count) * 31) / SCREEN_HEIGHT % 32;
            uint8_t b = 15;
            
            // Convert to BGR555 format
            uint16_t pixel = (b << 10) | (g << 5) | r;
            main_screen_[y * SCREEN_WIDTH + x] = pixel;
        }
    }
    
    // Sub screen: checkerboard pattern with animation
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            bool checker = ((x / 32) + (y / 32) + (frame_count / 30)) % 2;
            uint16_t pixel = checker ? 0x7FFF : 0x0000;  // White or black
            sub_screen_[y * SCREEN_WIDTH + x] = pixel;
        }
    }
}

uint16_t PPU::readDISPSTAT(bool is_main) const {
    // DISPSTAT register format:
    // Bit 0: VBlank flag (1 = in VBlank)
    // Bit 1: HBlank flag (0 for now, not implemented)
    // Bit 2: VCount match flag (1 = VCOUNT == VCOUNT match value)
    // Bits 3-7: Reserved
    // Bits 8-15: VCOUNT match value (LYC) - writable by game
    // CRITICAL: Read the stored DISPSTAT value from I/O register to get the match value
    // The game writes the match value to 0x04000004 (upper byte)
    // Use direct I/O read to avoid circular dependency (readDISPSTAT -> memory read -> readDISPSTAT)
    uint32_t dispstat_addr = is_main ? 0x04000004 : 0x04001004;
    uint16_t dispstat_stored = memory_->readIORegisterDirect(dispstat_addr);
    
    // Read the VCount match value from the upper byte of stored value (bits 8-15)
    uint8_t vcount_match = (dispstat_stored >> 8) & 0xFF;
    
    // Get current flags from our internal state
    uint16_t dispstat = is_main ? dispstat_main_ : dispstat_sub_;
    
    // CRITICAL: Check if current scanline matches the target
    // Pokemon waits for this to proceed past initialization
    if (scanline_ == vcount_match && vcount_match != 0) {
        dispstat |= 0x04;  // Set VCount match flag (bit 2)
    } else {
        dispstat &= ~0x04;  // Clear VCount match flag
    }
    
    // Combine: lower byte (flags) from our state, upper byte (match value) from stored value
    dispstat = (dispstat & 0x00FF) | (dispstat_stored & 0xFF00);
    
    return dispstat;
}

uint16_t PPU::readVCOUNT(bool is_main) const {
    // VCOUNT: Current scanline (0-261)
    // For now, return the current scanline
    // In a real implementation, this would be updated based on timing
    return static_cast<uint16_t>(scanline_);
}

void PPU::updateScanline(uint32_t scanline) {
    // Update scanline number directly (called from core based on cycle count)
    uint32_t old_scanline = scanline_;
    scanline_ = scanline;
    if (scanline_ >= 262) {
        scanline_ = 261;  // Clamp to last scanline
    }
    
    // Update DISPSTAT based on scanline
    if (scanline_ >= 192) {
        // VBlank period (scanlines 192-261)
        dispstat_main_ |= 0x01;  // Set VBlank flag
        dispstat_sub_ |= 0x01;
    } else {
        // Visible period (scanlines 0-191)
        dispstat_main_ &= ~0x01;  // Clear VBlank flag
        dispstat_sub_ &= ~0x01;
    }
    
    // CRITICAL: Update VCount Match flag
    // Read the match value from I/O register (upper byte of DISPSTAT at 0x04000004)
    // The VCount match value is stored in bits 8-15 of DISPSTAT (byte at offset 0x05)
    // Use direct I/O read to avoid circular dependency
    uint16_t dispstat_word = memory_->readIORegisterDirect(0x04000004);
    uint8_t vcount_match = (dispstat_word >> 8) & 0xFF;
    
    // Check if current scanline matches the target
    if (scanline_ == vcount_match && vcount_match != 0) {
        dispstat_main_ |= 0x04;  // Set VCount match flag (bit 2)
    } else {
        dispstat_main_ &= ~0x04;  // Clear VCount match flag
    }
    
    // Also update sub screen DISPSTAT
    uint16_t dispstat_sub_word = memory_->readIORegisterDirect(0x04001004);
    uint8_t vcount_match_sub = (dispstat_sub_word >> 8) & 0xFF;
    if (scanline_ == vcount_match_sub && vcount_match_sub != 0) {
        dispstat_sub_ |= 0x04;  // Set VCount match flag (bit 2)
    } else {
        dispstat_sub_ &= ~0x04;  // Clear VCount match flag
    }
    
    // CRITICAL: Trigger VBlank interrupt the MOMENT we cross scanline 192
    // This must happen during frame execution, not after
    if (old_scanline < 192 && scanline_ >= 192) {
        // Just entered VBlank - trigger interrupt immediately
        if (arm9_interrupts_) {
            arm9_interrupts_->requestInterrupt(InterruptController::INT_LCD_VBLANK);
        }
        if (arm7_interrupts_) {
            arm7_interrupts_->requestInterrupt(InterruptController::INT_LCD_VBLANK);
        }
    }
}

void PPU::convertBGR555ToRGB888(const uint16_t* src, uint8_t* dst, int width, int height) {
    for (int i = 0; i < width * height; i++) {
        uint16_t bgr555 = src[i];
        uint8_t r = ((bgr555 >> 0) & 0x1F) << 3;
        uint8_t g = ((bgr555 >> 5) & 0x1F) << 3;
        uint8_t b = ((bgr555 >> 10) & 0x1F) << 3;
        
        dst[i * 3 + 0] = r;
        dst[i * 3 + 1] = g;
        dst[i * 3 + 2] = b;
    }
}
