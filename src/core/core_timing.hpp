#pragma once

#include "../common/common_types.hpp"

/// Core timing - tracks cycles for frame pacing and future event scheduling.
/// ARM11 runs at ~268MHz; we use cycles as the base unit.
class CoreTiming {
public:
    CoreTiming() = default;

    void reset();
    void addCycles(u64 cycles) { current_cycles_ += cycles; }
    u64 getCurrentCycles() const { return current_cycles_; }
    void setCycles(u64 cycles) { current_cycles_ = cycles; }

    /// Target cycles per frame at 60 FPS (268MHz / 60 ~= 4.47M).
    /// Higher value = more instructions per frame = faster boot through init loops.
    static constexpr u64 kCyclesPerFrame = 4'466'667;
    u64 getCyclesToNextFrame() const;
    void advanceToNextFrame();

private:
    u64 current_cycles_{0};
    u64 next_frame_cycles_{kCyclesPerFrame};
};
