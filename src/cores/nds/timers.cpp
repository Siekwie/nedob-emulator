#include "timers.hpp"
#include "interrupts.hpp"
#include <cstdio>

TimerSystem::TimerSystem(InterruptController* interrupts, bool is_arm9)
    : interrupts_(interrupts)
    , is_arm9_(is_arm9)
{
    reset();
}

TimerSystem::~TimerSystem() {
}

void TimerSystem::reset() {
    for (int i = 0; i < 4; i++) {
        timers_[i].counter = 0;
        timers_[i].reload = 0;
        timers_[i].control = 0;
        timers_[i].enabled = false;
        timers_[i].cascade = false;
        timers_[i].prescaler = 0;
        timers_[i].prescaler_counter = 0;
        timers_[i].irq_enabled = false;
    }
}

void TimerSystem::step(uint32_t cycles) {
    for (int i = 0; i < 4; i++) {
        if (timers_[i].enabled) {
            updateTimer(i, cycles);
        }
    }
}

void TimerSystem::updateTimer(int timer_num, uint32_t cycles) {
    Timer& timer = timers_[timer_num];
    
    // Handle cascade mode
    if (timer.cascade && timer_num > 0) {
        // Cascade from previous timer - only increment when previous overflows
        // For now, we'll handle this in the main step loop
        return;
    }
    
    // Update prescaler
    int divisor = getPrescalerDivisor(timer.prescaler);
    timer.prescaler_counter += cycles;
    
    while (timer.prescaler_counter >= divisor) {
        timer.prescaler_counter -= divisor;
        
        // Increment counter
        timer.counter++;
        
        // Check for overflow (16-bit counter)
        if (timer.counter == 0) {
            // Reload value
            timer.counter = timer.reload;
            
            // Request interrupt if enabled
            if (timer.irq_enabled) {
                interrupts_->requestInterrupt(
                    static_cast<InterruptController::InterruptType>(
                        InterruptController::INT_TIMER0 + timer_num
                    )
                );
            }
            
            // Cascade to next timer if enabled
            if (timer_num < 3 && timers_[timer_num + 1].cascade) {
                timers_[timer_num + 1].counter++;
                if (timers_[timer_num + 1].counter == 0) {
                    timers_[timer_num + 1].counter = timers_[timer_num + 1].reload;
                    if (timers_[timer_num + 1].irq_enabled) {
                        interrupts_->requestInterrupt(
                            static_cast<InterruptController::InterruptType>(
                                InterruptController::INT_TIMER0 + timer_num + 1
                            )
                        );
                    }
                }
            }
        }
    }
}

int TimerSystem::getPrescalerDivisor(int prescaler) const {
    switch (prescaler) {
        case 0: return 1;
        case 1: return 64;
        case 2: return 256;
        case 3: return 1024;
        default: return 1;
    }
}

uint16_t TimerSystem::readTMCNT(int timer_num) {
    if (timer_num < 0 || timer_num >= 4) {
        return 0;
    }
    return timers_[timer_num].control;
}

void TimerSystem::writeTMCNT(int timer_num, uint16_t value) {
    if (timer_num < 0 || timer_num >= 4) {
        return;
    }
    
    Timer& timer = timers_[timer_num];
    bool was_enabled = timer.enabled;
    
    timer.control = value;
    timer.prescaler = (value >> 0) & 0x3;
    timer.cascade = (value >> 2) & 0x1;
    timer.irq_enabled = (value >> 6) & 0x1;
    timer.enabled = (value >> 7) & 0x1;
    
    // If timer is being enabled, reset prescaler
    if (timer.enabled && !was_enabled) {
        timer.prescaler_counter = 0;
        timer.counter = timer.reload;
    }
}

uint16_t TimerSystem::readTMCNT_L(int timer_num) {
    if (timer_num < 0 || timer_num >= 4) {
        return 0;
    }
    return timers_[timer_num].counter;
}

void TimerSystem::writeTMCNT_L(int timer_num, uint16_t value) {
    if (timer_num < 0 || timer_num >= 4) {
        return;
    }
    
    Timer& timer = timers_[timer_num];
    timer.reload = value;
    // If timer is not enabled, also set counter
    if (!timer.enabled) {
        timer.counter = value;
    }
}
