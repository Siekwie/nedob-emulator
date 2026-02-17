#include "ThreeDsCore.hpp"
#include "../arm/interpreter.hpp"
#include "../core_timing.hpp"
#include "../hle/svc.hpp"
#include "../loader/loader.hpp"
#include "../video/gpu.hpp"
#include "../video/gsp_hle.hpp"
#include "memory.hpp"
#include "../../common/logger.hpp"

ThreeDS::ThreeDS() = default;

ThreeDS::~ThreeDS() = default;

bool ThreeDS::loadROM(const std::string& path) {
    LoadResult load_result;
    if (loadNcch(path, load_result) != LoaderResult::Success) {
        Logger::log("ThreeDS: failed to load ROM: %s\n", path.c_str());
        return false;
    }

    memory_ = std::make_unique<MemorySystem>();
    if (load_result.use_segment_mapping && !load_result.segments.empty()) {
        for (const auto& seg : load_result.segments) {
            if (!seg.data.empty()) {
                memory_->mapCode(seg.vaddr, seg.data.data(), seg.data.size());
            }
        }
    } else {
        memory_->mapCode(load_result.text_address, load_result.code.data(), load_result.code.size());
    }
    memory_->initProcessEnvironmentBlock(load_result.program_id);
    memory_->write32(4, 0xE12FFF1Eu);  // Boot vector at 0x4: BX LR (dead end if PC lands here)

    timing_ = std::make_unique<CoreTiming>();
    timing_->reset();
    svc_dispatcher_ = std::make_unique<SvcDispatcher>();
    interpreter_ = std::make_unique<ArmInterpreter>(
        *memory_,
        [this](u32 svc_num) {
            return svc_dispatcher_->call(svc_num, *interpreter_);
        });

    interpreter_->setPC(load_result.entry_point);
    // Stack grows downward; start at the top of our dedicated stack region.
    // Using 0x0FB00000 underflows quickly when userland pops small frames.
    interpreter_->setSP(STACK_REGION_VADDR_END); // R13
    interpreter_->setLR(0);                    // R14 = link register (set by first BL/BLX)

    gpu_ = std::make_unique<VideoCore::Gpu>(*memory_);
    gsp_hle_ = std::make_unique<GspHle>(*memory_, *gpu_);
    svc_dispatcher_->setHandler(0x2D, [this](u32, ArmInterpreter& cpu) {
        return gsp_hle_->handleConnectToPort(cpu);
    });
    svc_dispatcher_->setHandler(0x32, [this](u32, ArmInterpreter& cpu) {
        return gsp_hle_->handleSendSyncRequest(cpu);
    });

    rom_path_ = path;
    rom_loaded_ = true;
    frame_count_ = 0;
    execution_stopped_ = false;

    Logger::log("ThreeDS: ROM loaded: %s (entry=0x%08X, code=%zu bytes)\n",
                path.c_str(), load_result.entry_point, load_result.code.size());
    return true;
}

void ThreeDS::runFrame() {
    if (!rom_loaded_ || paused_ || execution_stopped_) {
        return;
    }
    memory_->resetUnmappedLogCount();
    memory_->advanceSharedPageTick();

    if (frame_count_ % 30 == 0 && frame_count_ > 0) {
        const u32 pc = interpreter_->getPC();
        const auto& s = interpreter_->state();
        const u32 n = (s.cpsr >> 31) & 1, z = (s.cpsr >> 30) & 1, c = (s.cpsr >> 29) & 1, v = (s.cpsr >> 28) & 1;
        Logger::logInfo("Frame %llu PC=0x%08X R0=0x%08X R1=0x%08X R2=0x%08X NZCV=%u%u%u%u\n",
                        static_cast<unsigned long long>(frame_count_), pc, s.r[0], s.r[1], s.r[2], n, z, c, v);
    }

    const u64 cycles_limit = timing_->getCyclesToNextFrame();
    if (cycles_limit == 0) {
        timing_->advanceToNextFrame();
        ++frame_count_;
        return;
    }
    const u64 cycles_used = interpreter_->runSliceWithCycles(cycles_limit);
    timing_->addCycles(cycles_used);
    if (cycles_used == 0) {
        execution_stopped_ = true;
        Logger::logInfo("Emulation stopped at frame %llu (SVC returned stop)\n",
                        static_cast<unsigned long long>(frame_count_));
    }
    timing_->advanceToNextFrame();
    ++frame_count_;
}

void ThreeDS::reset() {
    if (rom_loaded_ && !rom_path_.empty()) {
        memory_.reset();
        interpreter_.reset();
        svc_dispatcher_.reset();
        timing_.reset();
        gpu_.reset();
        gsp_hle_.reset();
        loadROM(rom_path_);
    }
}

void ThreeDS::setDisplay(SDL_Window* window, SDL_Renderer* renderer) {
    if (gpu_) {
        gpu_->setWindow(window, renderer);
    }
}

void ThreeDS::present(SDL_Renderer* renderer) {
    if (gpu_) {
        gpu_->present(renderer);
    }
}
