#pragma once

#include "../../common/common_types.hpp"
#include <array>

namespace Gsp {

enum class CommandId : u32 {
    RequestDma = 0x00,
    SubmitCmdList = 0x01,
    MemoryFill = 0x02,
    DisplayTransfer = 0x03,
    TextureCopy = 0x04,
    CacheFlush = 0x05,
};

struct DmaCommand {
    u32 source_address;
    u32 dest_address;
    u32 size;
};

struct SubmitCmdListCommand {
    u32 address;
    u32 size;
    u32 flags;
    u32 unused[3];
    u32 do_flush;
};

struct MemoryFillCommand {
    u32 start1;
    u32 value1;
    u32 end1;
    u32 start2;
    u32 value2;
    u32 end2;
    u16 control1;
    u16 control2;
};

struct DisplayTransferCommand {
    u32 in_buffer_address;
    u32 out_buffer_address;
    u32 in_buffer_size;
    u32 out_buffer_size;
    u32 flags;
};

struct TextureCopyCommand {
    u32 in_buffer_address;
    u32 out_buffer_address;
    u32 size;
    u32 in_width_gap;
    u32 out_width_gap;
    u32 flags;
};

struct CacheFlushCommand {
    struct { u32 address; u32 size; } regions[3];
};

struct Command {
    u32 id;
    union {
        DmaCommand dma_request;
        SubmitCmdListCommand submit_gpu_cmdlist;
        MemoryFillCommand memory_fill;
        DisplayTransferCommand display_transfer;
        TextureCopyCommand texture_copy;
        CacheFlushCommand cache_flush;
        std::array<u8, 0x1C> raw_data;
    };
};
static_assert(sizeof(Command) >= 0x20, "Command size");

struct FrameBufferInfo {
    u32 active_fb;
    u32 address_left;
    u32 address_right;
    u32 stride;
    u32 format;
    u32 shown_fb;
    u32 unknown;
};
static_assert(sizeof(FrameBufferInfo) == 0x1c, "FrameBufferInfo size");

struct FrameBufferUpdate {
    u8 index;
    u8 is_dirty;
    u16 pad1;
    FrameBufferInfo framebuffer_info[2];
    u32 pad2;
};
static_assert(sizeof(FrameBufferUpdate) == 0x40, "FrameBufferUpdate size");

struct CommandBuffer {
    static constexpr u32 STATUS_STOPPED = 0x1;
    static constexpr u32 STATUS_CMD_FAILED = 0x80;

    u32 index;
    u32 number_commands;
    u32 status;
    u32 should_stop;
    u32 unk[4];
    Command commands[15];
};
static_assert(sizeof(CommandBuffer) == 0x200, "CommandBuffer size");

constexpr u32 FRAMEBUFFER_WIDTH = 240;
constexpr u32 TOP_FRAMEBUFFER_HEIGHT = 400;
constexpr u32 BOTTOM_FRAMEBUFFER_HEIGHT = 320;

}  // namespace Gsp
