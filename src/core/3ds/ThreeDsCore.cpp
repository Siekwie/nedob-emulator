#include "ThreeDsCore.hpp"
#include "../arm/interpreter.hpp"
#include "../core_timing.hpp"
#include "../hle/svc.hpp"
#include "../loader/loader.hpp"
#include "../video/gpu.hpp"
#include "../video/gsp_hle.hpp"
#include "memory.hpp"
#include "../../common/logger.hpp"
#include <cstdlib>

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
        const bool log_segs = (std::getenv("NEDOB_LOG_SEGMENTS") != nullptr);
        if (log_segs) {
            Logger::log("ThreeDS: mapping %zu segments:\n", load_result.segments.size());
        }
        for (const auto& seg : load_result.segments) {
            if (!seg.data.empty()) {
                if (log_segs) {
                    Logger::log("  seg vaddr=0x%08X size=%zu\n", seg.vaddr, seg.data.size());
                }
                memory_->mapCode(seg.vaddr, seg.data.data(), seg.data.size());
            }
        }
    } else {
        memory_->mapCode(load_result.text_address, load_result.code.data(), load_result.code.size());
    }
    // Mark only the text region executable. This prevents accidental BLX/BX into ro/data blobs.
    if (load_result.text_size > 0) {
        memory_->registerExecutableRange(load_result.text_address, load_result.text_size);
    } else if (!load_result.code.empty()) {
        memory_->registerExecutableRange(load_result.text_address, load_result.code.size());
    }
    memory_->initProcessEnvironmentBlock(load_result.program_id);
    memory_->write32(4, 0xE12FFF1Eu);  // Boot vector at 0x4: BX LR (dead end if PC lands here)

    // Bringup helper for Pokemon Sun worker loop:
    // At 0x002E7608 the routine returns 0 and main-thread code spins forever waiting for
    // progress that would normally come from full scheduler/thread semantics.
    // Force a non-zero return to move execution into the next startup stage.
    if (const char* v = std::getenv("NEDOB_PATCH_WORKER_RET1"); v && v[0] == '1') {
        const u32 at = 0x002E7608u;
        const u32 old = memory_->read32(at);
        if (old == 0xE3A00000u) { // MOV R0, #0
            memory_->write32(at, 0xE3A00001u); // MOV R0, #1
            Logger::log("Worker patch: wrote 0xE3A00001 @0x%08X\n", at);
        }
    }
    if (const char* v = std::getenv("NEDOB_PATCH_WORKER_LOOP_BNE"); v && v[0] == '1') {
        const u32 at = 0x002E7604u;
        const u32 old = memory_->read32(at);
        if (old == 0x1AFFFFF5u) { // BNE 0x002E75E0
            memory_->write32(at, 0xE1A00000u); // NOP
            Logger::log("Worker patch: wrote NOP @0x%08X\n", at);
        }
    }
    // Main thread loop at 0x00104560: BL check -> CMP R0,#0 -> BNE exit. If check returns 0, we
    // loop forever. Patch BNE to B so we always exit after first check.
    if (const char* v = std::getenv("NEDOB_PATCH_MAIN_LOOP_EXIT"); v && v[0] == '1') {
        const u32 at = 0x00104568u;
        const u32 old = memory_->read32(at);
        if (old == 0x1B000076u) {  // BNE +0x76*4 (exit loop)
            memory_->write32(at, 0xEA000076u);  // B +0x76*4 (always exit)
            Logger::log("Main loop patch: wrote B @0x%08X\n", at);
        }
    }
    // At 0x00104854: SUBS R0,... / 0x00104858: BPL +20. If BPL not taken we go to panic.
    // Patch BPL to B so we always skip the panic and continue at 0x00104874.
    if (const char* v = std::getenv("NEDOB_PATCH_TIMING_CHECK"); v && v[0] == '1') {
        const u32 at = 0x00104858u;
        const u32 old = memory_->read32(at);
        if (old == 0x5A000005u) {  // BPL +5*4
            memory_->write32(at, 0xEA000005u);  // B +5*4 (always skip panic)
            Logger::log("Timing check patch: wrote B @0x%08X\n", at);
        }
    }

    // Dev helper: dump a few words from mapped memory to quickly sanity-check
    // code/rodata placement without editing code.
    // Format: NEDOB_DUMP_WORDS="0xADDR,COUNT" (COUNT is number of 32-bit words).
    if (const char* v = std::getenv("NEDOB_DUMP_WORDS"); v && v[0] != '\0') {
        unsigned long addr_ul = 0;
        unsigned long count_ul = 0;
        if (std::sscanf(v, "%lx,%lu", &addr_ul, &count_ul) == 2 && count_ul > 0 && count_ul <= 256ul) {
            const u32 addr = static_cast<u32>(addr_ul);
            const u32 count = static_cast<u32>(count_ul);
            Logger::log("NEDOB_DUMP_WORDS: addr=0x%08X count=%u\n", addr, count);
            for (u32 i = 0; i < count; ++i) {
                const u32 a = addr + i * 4u;
                const u32 w = memory_->read32(a);
                Logger::log("  [0x%08X] = 0x%08X\n", a, w);
            }
        }
    }

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
    // Leave a small slack page below TLS so post-increment pops can't step into the TLS page.
    interpreter_->setSP(STACK_REGION_VADDR_END - 0x1000u); // R13
    interpreter_->setLR(0);                    // R14 = link register (set by first BL/BLX)

    // Bringup hook: allow forcing initial callee-saved regs for titles whose crt0 assumes
    // a non-zero ABI context in R5/R6. Keep it env-gated.
    if (const char* v = std::getenv("NEDOB_BOOT_R5"); v && v[0] != '\0') {
        const unsigned long r5 = std::strtoul(v, nullptr, 0);
        interpreter_->state().r[5] = static_cast<u32>(r5);
    }
    if (const char* v = std::getenv("NEDOB_BOOT_R6"); v && v[0] != '\0') {
        const unsigned long r6 = std::strtoul(v, nullptr, 0);
        interpreter_->state().r[6] = static_cast<u32>(r6);
    }

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
    if (svc_dispatcher_) {
        u32 max_bootstraps = 1;
        if (const char* v = std::getenv("NEDOB_BOOTSTRAP_THREADS_PER_FRAME"); v && v[0] != '\0') {
            max_bootstraps = static_cast<u32>(std::strtoul(v, nullptr, 0));
            if (max_bootstraps == 0) max_bootstraps = 1;
        }
        svc_dispatcher_->runPendingThreadBootstraps(*interpreter_, max_bootstraps);
    }

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
