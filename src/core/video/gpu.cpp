#include "gpu.hpp"
#include "../../common/logger.hpp"
#include "../3ds/memory.hpp"
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <cstring>
#include <algorithm>

namespace VideoCore {

namespace {

constexpr VAddr GPU_REG_BASE = 0x1EF00000;
constexpr VAddr DEFAULT_TOP_FB = 0x1F000000;
constexpr VAddr DEFAULT_BOTTOM_FB = 0x1F0A000;
constexpr u32 PIXEL_FORMAT_RGBA8 = 0;
constexpr u32 PIXEL_FORMAT_RGB8 = 1;
constexpr u32 PIXEL_FORMAT_RGB565 = 2;
constexpr u32 PIXEL_FORMAT_RGB5A1 = 3;
constexpr u32 PIXEL_FORMAT_RGBA4 = 4;

void copyLinearToRgba32(const u8* src, u8* dst, u32 width, u32 height, u32 stride, u32 format) {
    auto bytesPerPixel = [](u32 fmt) {
        switch (fmt & 7) {
            case PIXEL_FORMAT_RGBA8: return 4u;
            case PIXEL_FORMAT_RGB8: return 3u;
            default: return 2u;
        }
    };
    const u32 bpp = bytesPerPixel(format);
    const u32 src_stride = stride > 0 ? stride : width * bpp;

    for (u32 y = 0; y < height; ++y) {
        const u8* row_src = src + y * src_stride;
        u8* row_dst = dst + y * width * 4;
        for (u32 x = 0; x < width; ++x) {
            u8 r = 0, g = 0, b = 0, a = 255;
            switch (format & 7) {
                case PIXEL_FORMAT_RGBA8:
                    r = row_src[0]; g = row_src[1]; b = row_src[2]; a = row_src[3];
                    row_src += 4;
                    break;
                case PIXEL_FORMAT_RGB8:
                    r = row_src[0]; g = row_src[1]; b = row_src[2];
                    row_src += 3;
                    break;
                case PIXEL_FORMAT_RGB565: {
                    u16 v = row_src[0] | (row_src[1] << 8);
                    r = ((v >> 11) & 0x1F) * 255 / 31;
                    g = ((v >> 5) & 0x3F) * 255 / 63;
                    b = (v & 0x1F) * 255 / 31;
                    row_src += 2;
                    break;
                }
                case PIXEL_FORMAT_RGB5A1: {
                    u16 v = row_src[0] | (row_src[1] << 8);
                    r = ((v >> 11) & 0x1F) * 255 / 31;
                    g = ((v >> 6) & 0x1F) * 255 / 31;
                    b = ((v >> 1) & 0x1F) * 255 / 31;
                    a = (v & 1) ? 255 : 0;
                    row_src += 2;
                    break;
                }
                case PIXEL_FORMAT_RGBA4: {
                    u16 v = row_src[0] | (row_src[1] << 8);
                    r = ((v >> 12) & 0xF) * 17;
                    g = ((v >> 8) & 0xF) * 17;
                    b = ((v >> 4) & 0xF) * 17;
                    a = (v & 0xF) * 17;
                    row_src += 2;
                    break;
                }
                default:
                    row_src += bpp;
                    break;
            }
            row_dst[0] = r; row_dst[1] = g; row_dst[2] = b; row_dst[3] = a;
            row_dst += 4;
        }
    }
}

}  // namespace

Gpu::Gpu(MemorySystem& memory) : memory_(memory) {}

Gpu::~Gpu() = default;

void Gpu::setWindow(SDL_Window* window, SDL_Renderer* renderer) {
    window_ = window;
    renderer_ = renderer;
}

void Gpu::executeCommand(const Gsp::Command& cmd) {
    using Gsp::CommandId;
    const auto id = static_cast<CommandId>(cmd.id & 0xFF);
    if (id == CommandId::MemoryFill || id == CommandId::DisplayTransfer) {
        Logger::logInfo("GPU: cmd 0x%08X (MemoryFill/DisplayTransfer)\n", cmd.id);
    }
    switch (id) {
        case CommandId::SubmitCmdList:
            doSubmitCmdList(cmd.submit_gpu_cmdlist);
            break;
        case CommandId::MemoryFill:
            if (cmd.memory_fill.start1 != 0) {
                doMemoryFill(cmd.memory_fill.start1, cmd.memory_fill.end1,
                            cmd.memory_fill.value1, cmd.memory_fill.control1);
            }
            if (cmd.memory_fill.start2 != 0) {
                doMemoryFill(cmd.memory_fill.start2, cmd.memory_fill.end2,
                            cmd.memory_fill.value2, cmd.memory_fill.control2);
            }
            break;
        case CommandId::DisplayTransfer:
            doDisplayTransfer(cmd.display_transfer);
            break;
        case CommandId::RequestDma:
        case CommandId::TextureCopy:
        case CommandId::CacheFlush:
            break;
        default:
            break;
    }
}

void Gpu::writeHwRegs(VAddr addr, const u32* data, u32 count) {
    (void)addr;
    (void)data;
    (void)count;
}

void Gpu::writeHwRegsWithMask(VAddr addr, const u32* data, const u32* mask, u32 count) {
    (void)addr;
    (void)data;
    (void)mask;
    (void)count;
}

u32 Gpu::readHwReg(VAddr addr) {
    (void)addr;
    return 0;
}

void Gpu::setBufferSwap(u32 screen_id, const Gsp::FrameBufferInfo& info) {
    if (screen_id == 0) {
        top_fb_info_[info.active_fb & 1] = info;
        top_shown_fb_ = info.shown_fb & 1;
        top_dirty_ = true;
    } else {
        bottom_fb_info_[info.active_fb & 1] = info;
        bottom_shown_fb_ = info.shown_fb & 1;
        bottom_dirty_ = true;
    }
}

void Gpu::setFrameBufferUpdate(u32 /*thread_id*/, u32 screen_id, const Gsp::FrameBufferUpdate* update) {
    if (!update) return;
    for (int i = 0; i < 2; ++i) {
        if (screen_id == 0) {
            top_fb_info_[i] = update->framebuffer_info[i];
            top_shown_fb_ = update->framebuffer_info[update->index & 1].shown_fb & 1;
        } else {
            bottom_fb_info_[i] = update->framebuffer_info[i];
            bottom_shown_fb_ = update->framebuffer_info[update->index & 1].shown_fb & 1;
        }
    }
    if (screen_id == 0) top_dirty_ = true; else bottom_dirty_ = true;
}

bool Gpu::hasFramebuffer(u32 screen_id) const {
    if (screen_id == 0) {
        return top_fb_info_[top_shown_fb_].address_left != 0;
    }
    return bottom_fb_info_[bottom_shown_fb_].address_left != 0;
}

void Gpu::doMemoryFill(u32 start, u32 end, u32 value, u16 control) {
    const VAddr vaddr = memory_.virtualToPhysical(start);
    const VAddr vend = memory_.virtualToPhysical(end);
    if (vend <= vaddr) return;
    const std::size_t size = vend - vaddr;
    u8* ptr = memory_.getVramPointer(start);
    if (!ptr) {
        ptr = memory_.getVramPointer(vaddr);
    }
    if (!ptr) return;
    const bool fill_32 = (control >> 9) & 1;
    const bool fill_24 = (control >> 8) & 1;
    if (fill_32) {
        std::size_t n = size / 4;
        u32* p = reinterpret_cast<u32*>(ptr);
        for (std::size_t i = 0; i < n; ++i) p[i] = value;
    } else if (fill_24) {
        std::size_t n = size / 3;
        for (std::size_t i = 0; i < n; ++i) {
            ptr[i * 3 + 0] = static_cast<u8>(value);
            ptr[i * 3 + 1] = static_cast<u8>(value >> 8);
            ptr[i * 3 + 2] = static_cast<u8>(value >> 16);
        }
    } else {
        std::size_t n = size / 2;
        u16 v = static_cast<u16>(value);
        for (std::size_t i = 0; i < n; ++i) {
            ptr[i * 2] = static_cast<u8>(v);
            ptr[i * 2 + 1] = static_cast<u8>(v >> 8);
        }
    }
    if (start >= 0x1F000000u && start < 0x1F600000u) top_dirty_ = bottom_dirty_ = true;
}

void Gpu::doDisplayTransfer(const Gsp::DisplayTransferCommand& cmd) {
    const u8* src = memory_.getVramPointer(cmd.in_buffer_address);
    u8* dst = memory_.getVramPointer(cmd.out_buffer_address);
    if (!src || !dst) return;
    const u32 size = std::min(cmd.in_buffer_size, cmd.out_buffer_size);
    std::memcpy(dst, src, size);
    top_dirty_ = bottom_dirty_ = true;
}

void Gpu::doSubmitCmdList(const Gsp::SubmitCmdListCommand& cmd) {
    (void)cmd;
}

void Gpu::presentTopScreen(SDL_Renderer* renderer, int win_w, int win_h) {
    const auto& info = top_fb_info_[top_shown_fb_];
    VAddr fb_addr = info.address_left != 0 ? info.address_left : DEFAULT_TOP_FB;
    u32 fmt = info.address_left != 0 ? info.format : PIXEL_FORMAT_RGBA8;
    u32 stride = info.address_left != 0 ? info.stride : Gsp::FRAMEBUFFER_WIDTH * 4;
    const u32 w = Gsp::FRAMEBUFFER_WIDTH;
    const u32 h = Gsp::TOP_FRAMEBUFFER_HEIGHT;
    top_texture_.resize(w * h * 4);
    const u8* fb = memory_.getVramPointer(fb_addr);
    if (!fb) {
        static int warn_count = 0;
        if (warn_count++ < 5) {
            Logger::logInfo("GPU: presentTopScreen: no pointer for fb_addr=0x%08X\n", fb_addr);
        }
        return;
    }
    if (stride == 0) {
        stride = ((fmt & 7) == PIXEL_FORMAT_RGB8) ? w * 3 : w * 4;
    }
    copyLinearToRgba32(fb, top_texture_.data(), w, h, stride, fmt);
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, top_texture_.data(), static_cast<int>(w * 4));
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) return;
    float scale = std::min(static_cast<float>(win_w) / w, static_cast<float>(win_h) / h);
    int dw = static_cast<int>(w * scale);
    int dh = static_cast<int>(h * scale);
    SDL_FRect dst{0, 0, static_cast<float>(dw), static_cast<float>(dh)};
    SDL_RenderTexture(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

void Gpu::presentBottomScreen(SDL_Renderer* renderer, int win_w, int win_h) {
    const auto& info = bottom_fb_info_[bottom_shown_fb_];
    VAddr fb_addr = info.address_left != 0 ? info.address_left : DEFAULT_BOTTOM_FB;
    u32 fmt = info.address_left != 0 ? info.format : PIXEL_FORMAT_RGBA8;
    u32 stride = info.address_left != 0 ? info.stride : Gsp::FRAMEBUFFER_WIDTH * 4;
    const u32 w = Gsp::FRAMEBUFFER_WIDTH;
    const u32 h = Gsp::BOTTOM_FRAMEBUFFER_HEIGHT;
    bottom_texture_.resize(w * h * 4);
    const u8* fb = memory_.getVramPointer(fb_addr);
    if (!fb) {
        static int warn_count = 0;
        if (warn_count++ < 5) {
            Logger::logInfo("GPU: presentBottomScreen: no pointer for fb_addr=0x%08X\n", fb_addr);
        }
        return;
    }
    if (stride == 0) {
        stride = ((fmt & 7) == PIXEL_FORMAT_RGB8) ? w * 3 : w * 4;
    }
    copyLinearToRgba32(fb, bottom_texture_.data(), w, h, stride, fmt);
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, bottom_texture_.data(), static_cast<int>(w * 4));
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) return;
    int win_half = win_h / 2;
    float scale = std::min(static_cast<float>(win_w) / w, static_cast<float>(win_half) / h);
    int dw = static_cast<int>(w * scale);
    int dh = static_cast<int>(h * scale);
    SDL_FRect dst{0, static_cast<float>(win_half), static_cast<float>(dw), static_cast<float>(dh)};
    SDL_RenderTexture(renderer, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
}

void Gpu::present(SDL_Renderer* renderer) {
    if (!renderer || !window_) return;
    int w = 0, h = 0;
    SDL_GetWindowSize(window_, &w, &h);
    if (w <= 0 || h <= 0) return;
    SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
    SDL_RenderClear(renderer);
    const int half_h = h / 2;
    presentTopScreen(renderer, w, half_h);
    presentBottomScreen(renderer, w, half_h);
}

}  // namespace VideoCore
