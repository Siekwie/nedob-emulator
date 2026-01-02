#include "interrupts.hpp"
#include <cstdio>

InterruptController::InterruptController()
    : interrupt_enable_(0)
    , interrupt_flags_(0)
{
}

InterruptController::~InterruptController() {
}

void InterruptController::requestInterrupt(InterruptType type) {
    if (type < INT_COUNT) {
        interrupt_flags_ |= (1U << type);
        // Debug: Log VBlank interrupts (first few only)
        if (type == INT_LCD_VBLANK) {
            static int vblank_count = 0;
            if (vblank_count < 5) {
                std::printf("InterruptController: VBlank interrupt requested (flags=0x%08X)\n", interrupt_flags_);
                vblank_count++;
            }
        }
    }
}

bool InterruptController::hasPendingInterrupt() const {
    return (interrupt_enable_ & interrupt_flags_) != 0;
}

bool InterruptController::hasPendingInterrupt(InterruptType type) const {
    if (type >= INT_COUNT) return false;
    return (interrupt_enable_ & interrupt_flags_ & (1U << type)) != 0;
}

InterruptController::InterruptType InterruptController::getPendingInterrupt() const {
    uint32_t pending = interrupt_enable_ & interrupt_flags_;
    if (pending == 0) {
        return INT_COUNT;
    }
    
    // Find the lowest set bit (highest priority)
    for (int i = 0; i < INT_COUNT; i++) {
        if (pending & (1U << i)) {
            return static_cast<InterruptType>(i);
        }
    }
    
    return INT_COUNT;
}

void InterruptController::clearInterrupt(InterruptType type) {
    if (type < INT_COUNT) {
        interrupt_flags_ &= ~(1U << type);
    }
}
