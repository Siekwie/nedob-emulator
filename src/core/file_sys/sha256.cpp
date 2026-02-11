// Minimal SHA-256 (FIPS 180-2). Public domain style.

#include "sha256.hpp"
#include <cstring>

namespace Sha256 {

static const u32 K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static inline u32 rotr(u32 x, int n) { return (x >> n) | (x << (32 - n)); }
static inline u32 ch(u32 x, u32 y, u32 z) { return (x & y) ^ (~x & z); }
static inline u32 maj(u32 x, u32 y, u32 z) { return (x & y) ^ (x & z) ^ (y & z); }
static inline u32 sig0(u32 x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
static inline u32 sig1(u32 x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
static inline u32 gam0(u32 x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
static inline u32 gam1(u32 x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

static void be32(u8* out, u32 x) {
    out[0] = static_cast<u8>(x >> 24);
    out[1] = static_cast<u8>(x >> 16);
    out[2] = static_cast<u8>(x >> 8);
    out[3] = static_cast<u8>(x);
}

static u32 load32be(const u8* p) {
    return (static_cast<u32>(p[0]) << 24) | (static_cast<u32>(p[1]) << 16) |
           (static_cast<u32>(p[2]) << 8) | static_cast<u32>(p[3]);
}

static void processBlock(const u8* block, u32* H) {
    u32 W[64];
    for (int t = 0; t < 16; ++t)
        W[t] = load32be(block + t * 4);
    for (int t = 16; t < 64; ++t)
        W[t] = gam1(W[t-2]) + W[t-7] + gam0(W[t-15]) + W[t-16];

    u32 a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
    for (int t = 0; t < 64; ++t) {
        u32 T1 = h + sig1(e) + ch(e, f, g) + K[t] + W[t];
        u32 T2 = sig0(a) + maj(a, b, c);
        h = g; g = f; f = e; e = d + T1; d = c; c = b; b = a; a = T1 + T2;
    }
    H[0] += a; H[1] += b; H[2] += c; H[3] += d;
    H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

void hash(const u8* data, std::size_t len, u8* out) {
    u32 H[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    u8 block[64];
    std::size_t total_bits = len * 8;

    while (len >= 64) {
        std::memcpy(block, data, 64);
        processBlock(block, H);
        data += 64;
        len -= 64;
    }

    std::size_t rem = len;
    std::memcpy(block, data, rem);
    block[rem++] = 0x80;
    if (rem > 56) {
        std::memset(block + rem, 0, 64 - rem);
        processBlock(block, H);
        std::memset(block, 0, 56);
    } else {
        std::memset(block + rem, 0, 56 - rem);
    }
    be32(block + 56, static_cast<u32>(total_bits >> 32));
    be32(block + 60, static_cast<u32>(total_bits));
    processBlock(block, H);

    for (int i = 0; i < 8; ++i)
        be32(out + i * 4, H[i]);
}

}  // namespace Sha256
