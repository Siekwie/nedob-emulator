#pragma once

#include "../../common/common_types.hpp"
#include <array>

namespace Aes {

using AESKey = std::array<u8, 16>;

/// Left rotate 128-bit key by rot bits (0-127)
AESKey lrot128(const AESKey& in, u32 rot);

/// 128-bit addition (big-endian)
AESKey add128(const AESKey& a, const AESKey& b);
AESKey add128(const AESKey& a, u64 b);

/// 128-bit XOR
AESKey xor128(const AESKey& a, const AESKey& b);

/// Derive NormalKey from KeyX and KeyY: NormalKey = ROL(ADD(XOR(ROL(KeyX,2), KeyY), constant), 87)
AESKey deriveNormalKey(const AESKey& key_x, const AESKey& key_y, const AESKey& generator_constant);

}  // namespace Aes
