#include "io_registers.hpp"
#include "memory.hpp"
#include <cstdio>

// I/O Register Addresses
namespace {
    // Display Control
    constexpr uint32_t REG_DISPCNT_MAIN = 0x04000000;
    constexpr uint32_t REG_DISPCNT_SUB = 0x04001000;
    
    // Background Control (Main Screen)
    constexpr uint32_t REG_BG0CNT_MAIN = 0x04000008;
    constexpr uint32_t REG_BG1CNT_MAIN = 0x0400000A;
    constexpr uint32_t REG_BG2CNT_MAIN = 0x0400000C;
    constexpr uint32_t REG_BG3CNT_MAIN = 0x0400000E;
    
    // Background Control (Sub Screen)
    constexpr uint32_t REG_BG0CNT_SUB = 0x04001008;
    constexpr uint32_t REG_BG1CNT_SUB = 0x0400100A;
    constexpr uint32_t REG_BG2CNT_SUB = 0x0400100C;
    constexpr uint32_t REG_BG3CNT_SUB = 0x0400100E;
    
    // Input
    constexpr uint32_t REG_KEYINPUT = 0x04000130;
}

IORegisters::IORegisters(NDSMemory* memory)
    : memory_(memory)
    , main_display_control_(0)
    , sub_display_control_(0)
    , key_input_(0x03FF)  // All keys released (active low)
{
    for (int i = 0; i < 8; i++) {
        bg_control_[i] = 0;
    }
}

IORegisters::~IORegisters() {
}

uint32_t IORegisters::read32(uint32_t address) {
    // Handle 32-bit register reads
    if (address == REG_DISPCNT_MAIN) {
        return main_display_control_;
    }
    if (address == REG_DISPCNT_SUB) {
        return sub_display_control_;
    }
    if (address == REG_KEYINPUT) {
        return key_input_;
    }
    
    // For other registers, read from memory
    return memory_->read32_ARM9(address);
}

uint16_t IORegisters::read16(uint32_t address) {
    if (address == REG_DISPCNT_MAIN) {
        return main_display_control_;
    }
    if (address == REG_DISPCNT_SUB) {
        return sub_display_control_;
    }
    if (address == REG_KEYINPUT) {
        return key_input_;
    }
    if (address >= REG_BG0CNT_MAIN && address <= REG_BG3CNT_MAIN) {
        int bg = (address - REG_BG0CNT_MAIN) / 2;
        return bg_control_[bg];
    }
    if (address >= REG_BG0CNT_SUB && address <= REG_BG3CNT_SUB) {
        int bg = 4 + (address - REG_BG0CNT_SUB) / 2;
        return bg_control_[bg];
    }
    
    return memory_->read16_ARM9(address);
}

uint8_t IORegisters::read8(uint32_t address) {
    return static_cast<uint8_t>(read16(address & ~1) >> ((address & 1) * 8));
}

void IORegisters::write32(uint32_t address, uint32_t value) {
    handleRegisterWrite(address, value);
    memory_->write32_ARM9(address, value);
}

void IORegisters::write16(uint32_t address, uint16_t value) {
    if (address == REG_DISPCNT_MAIN) {
        main_display_control_ = value;
        return;
    }
    if (address == REG_DISPCNT_SUB) {
        sub_display_control_ = value;
        return;
    }
    if (address >= REG_BG0CNT_MAIN && address <= REG_BG3CNT_MAIN) {
        int bg = (address - REG_BG0CNT_MAIN) / 2;
        bg_control_[bg] = value;
        return;
    }
    if (address >= REG_BG0CNT_SUB && address <= REG_BG3CNT_SUB) {
        int bg = 4 + (address - REG_BG0CNT_SUB) / 2;
        bg_control_[bg] = value;
        return;
    }
    
    memory_->write16_ARM9(address, value);
}

void IORegisters::write8(uint32_t address, uint8_t value) {
    uint16_t current = read16(address & ~1);
    if (address & 1) {
        current = (current & 0x00FF) | (static_cast<uint16_t>(value) << 8);
    } else {
        current = (current & 0xFF00) | value;
    }
    write16(address & ~1, current);
}

void IORegisters::handleRegisterWrite(uint32_t address, uint32_t value) {
    // Handle special register behaviors
    // For now, most registers just get written to memory
}
