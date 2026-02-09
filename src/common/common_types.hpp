#pragma once

#include <cstdint>

/// 3DS/emulator common types (aligned with Azahar-style addressing).
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

/// Virtual address in ARM11 user space.
using VAddr = u32;
/// Physical address in ARM11 physical address space.
using PAddr = u32;
