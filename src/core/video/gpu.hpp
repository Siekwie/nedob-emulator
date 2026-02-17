#pragma once

#include "../../common/common_types.hpp"
#include "../3ds/memory.hpp"
#include "gsp_types.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace VideoCore {

class Gpu {
public:
    explicit Gpu(MemorySystem& memory);
    ~Gpu();

    void setWindow(SDL_Window* window, SDL_Renderer* renderer);

    void executeCommand(const Gsp::Command& cmd);

    void writeHwRegs(VAddr addr, const u32* data, u32 count);
    void writeHwRegsWithMask(VAddr addr, const u32* data, const u32* mask, u32 count);
    u32 readHwReg(VAddr addr);

    void setBufferSwap(u32 screen_id, const Gsp::FrameBufferInfo& info);

    void setFrameBufferUpdate(u32 thread_id, u32 screen_id, const Gsp::FrameBufferUpdate* update);

    bool hasFramebuffer(u32 screen_id) const;
    void present(SDL_Renderer* renderer);

private:
    void doMemoryFill(u32 start, u32 end, u32 value, u16 control);
    void doDisplayTransfer(const Gsp::DisplayTransferCommand& cmd);
    void doSubmitCmdList(const Gsp::SubmitCmdListCommand& cmd);

    void presentTopScreen(SDL_Renderer* renderer, int win_w, int win_h);
    void presentBottomScreen(SDL_Renderer* renderer, int win_w, int win_h);

    MemorySystem& memory_;
    SDL_Window* window_{nullptr};
    SDL_Renderer* renderer_{nullptr};

    std::array<Gsp::FrameBufferInfo, 2> top_fb_info_{};
    std::array<Gsp::FrameBufferInfo, 2> bottom_fb_info_{};
    u32 top_shown_fb_{0};
    u32 bottom_shown_fb_{0};
    bool top_dirty_{false};
    bool bottom_dirty_{false};

    std::vector<u8> top_texture_;
    std::vector<u8> bottom_texture_;
};

}  // namespace VideoCore
