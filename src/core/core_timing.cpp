#include "core_timing.hpp"

void CoreTiming::reset() {
    current_cycles_ = 0;
    next_frame_cycles_ = kCyclesPerFrame;
}

u64 CoreTiming::getCyclesToNextFrame() const {
    if (current_cycles_ >= next_frame_cycles_) {
        return 0;
    }
    return next_frame_cycles_ - current_cycles_;
}

void CoreTiming::advanceToNextFrame() {
    next_frame_cycles_ += kCyclesPerFrame;
}
