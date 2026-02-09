#include "ThreeDsCore.hpp"
#include <cstdio>
#include <fstream>

ThreeDS::ThreeDS() = default;

ThreeDS::~ThreeDS() = default;

bool ThreeDS::loadROM(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "ThreeDS: could not open ROM: %s\n", path.c_str());
        return false;
    }
    auto size = f.tellg();
    f.close();

    rom_path_ = path;
    rom_loaded_ = true;
    frame_count_ = 0;

    std::fprintf(stderr, "ThreeDS: ROM loaded: %s (%lld bytes)\n", path.c_str(),
                 static_cast<long long>(size));
    return true;
}

void ThreeDS::runFrame() {
    if (!rom_loaded_ || paused_) {
        return;
    }
    // TODO: run ARM11 core(s), timing, then GPU/display sync
    ++frame_count_;
}

void ThreeDS::reset() {
    if (rom_loaded_ && !rom_path_.empty()) {
        frame_count_ = 0;
        // TODO: re-init memory, CPU state, reload ROM
    }
}
