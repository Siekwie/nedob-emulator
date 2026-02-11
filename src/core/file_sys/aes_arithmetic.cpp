#include "aes_arithmetic.hpp"
#include <algorithm>
#include <functional>

namespace Aes {

AESKey lrot128(const AESKey& in, u32 rot) {
    AESKey out{};
    rot %= 128;
    const u32 byte_shift = rot / 8;
    const u32 bit_shift = rot % 8;

    for (u32 i = 0; i < 16; i++) {
        const u32 wrap_a = (i + byte_shift) % 16;
        const u32 wrap_b = (i + byte_shift + 1) % 16;
        out[i] = static_cast<u8>(((in[wrap_a] << bit_shift) | (in[wrap_b] >> (8 - bit_shift))) & 0xFF);
    }
    return out;
}

AESKey add128(const AESKey& a, const AESKey& b) {
    AESKey out{};
    u32 carry = 0;
    for (int i = 15; i >= 0; i--) {
        const u32 sum = a[i] + b[i] + carry;
        carry = sum >> 8;
        out[i] = static_cast<u8>(sum & 0xFF);
    }
    return out;
}

AESKey add128(const AESKey& a, u64 b) {
    AESKey out = a;
    u32 carry = 0;
    for (int i = 15; i >= 8; i--) {
        const u32 sum = a[i] + static_cast<u8>((b >> ((15 - i) * 8)) & 0xFF) + carry;
        carry = sum >> 8;
        out[i] = static_cast<u8>(sum & 0xFF);
    }
    return out;
}

AESKey xor128(const AESKey& a, const AESKey& b) {
    AESKey out;
    std::transform(a.cbegin(), a.cend(), b.cbegin(), out.begin(), std::bit_xor<>());
    return out;
}

AESKey deriveNormalKey(const AESKey& key_x, const AESKey& key_y, const AESKey& generator_constant) {
    return lrot128(add128(xor128(lrot128(key_x, 2), key_y), generator_constant), 87);
}

}  // namespace Aes
