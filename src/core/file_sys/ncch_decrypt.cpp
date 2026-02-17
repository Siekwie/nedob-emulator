#include "ncch_decrypt.hpp"
#include "aes_arithmetic.hpp"
#include "ncch.hpp"
#include "sha256.hpp"
#include "../../common/logger.hpp"
extern "C" {
#include "aes.h"
}
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

constexpr u32 kBlockSize = 0x200;
constexpr u32 kExeFsHeaderSize = 0x200;
constexpr u32 kMaxSections = 8;
constexpr u8 kSeedCryptoFlag = 0x20;  // NCCH crypto_info bit 5 = seed encryption (7.x+)

// NCCH slot IDs for key derivation
constexpr std::size_t kNCCHSecure1 = 0x2C;
constexpr std::size_t kNCCHSecure2 = 0x25;
constexpr std::size_t kNCCHSecure3 = 0x18;
constexpr std::size_t kNCCHSecure4 = 0x1B;

std::array<u8, 16> g_slot_2c_normal_key{};
std::array<u8, 16> g_slot_25_normal_key{};
std::array<u8, 16> g_slot_18_normal_key{};
std::array<u8, 16> g_slot_1b_normal_key{};
std::array<u8, 16> g_slot_2c_key_x{};
std::array<u8, 16> g_slot_25_key_x{};
std::array<u8, 16> g_slot_18_key_x{};
std::array<u8, 16> g_slot_1b_key_x{};
std::array<u8, 16> g_generator_constant{};
bool g_keys_loaded = false;
bool g_has_key_x_2c = false;
bool g_has_key_x_25 = false;
bool g_has_key_x_18 = false;
bool g_has_key_x_1b = false;

bool parseHexKey(const std::string& value, std::array<u8, 16>& out) {
    if (value.size() < 32) return false;
    for (size_t i = 0; i < 16 && (i * 2 + 1) < value.size(); ++i) {
        unsigned byte = 0;
        if (std::sscanf(value.c_str() + i * 2, "%2x", &byte) != 1) return false;
        out[i] = static_cast<u8>(byte);
    }
    return true;
}

std::size_t slotFromSecondary(u8 secondary_key_slot) {
    switch (secondary_key_slot) {
        case 0: return kNCCHSecure1;
        case 1: return kNCCHSecure2;
        case 10: return kNCCHSecure3;
        case 11: return kNCCHSecure4;
        default: return kNCCHSecure1;
    }
}

const u8* getNormalKey(std::size_t slot_id) {
    switch (slot_id) {
        case kNCCHSecure1: return g_slot_2c_normal_key.data();
        case kNCCHSecure2: return g_slot_25_normal_key.data();
        case kNCCHSecure3: return g_slot_18_normal_key.data();
        case kNCCHSecure4: return g_slot_1b_normal_key.data();
        default: return g_slot_2c_normal_key.data();
    }
}

/// Build section CTR for NCCH version 0/2. IV[0..7]=partition_id reversed, IV[8]=type, IV[9..15]=0.
void buildSectionCtr(const u8* partition_id, u8 type, std::array<u8, 16>& ctr) {
    std::reverse_copy(partition_id, partition_id + 8, ctr.begin());
    ctr[8] = type;  // 1=Exheader, 2=ExeFS, 3=RomFS
    std::fill(ctr.begin() + 9, ctr.end(), 0);
}

/// Add block_offset (in 16-byte units) to the IV counter (bytes 9..15, big-endian).
void addBlockOffsetToCtr(std::array<u8, 16>& ctr, u64 block_offset) {
    u64 carry = block_offset;
    for (int i = 15; i >= 9 && carry > 0; i--) {
        carry += ctr[i];
        ctr[i] = static_cast<u8>(carry & 0xFF);
        carry >>= 8;
    }
}

/// Decrypt in-place using AES-128-CTR.
void decryptCtr(const u8* key, std::array<u8, 16>& ctr, u8* data, std::size_t size, u64 block_offset) {
    addBlockOffsetToCtr(ctr, block_offset);
    struct AES_ctx ctx;
    AES_init_ctx_iv(&ctx, key, ctr.data());
    AES_CTR_xcrypt_buffer(&ctx, data, size);
}

}  // namespace

bool loadAesKeys(const std::string& sysdata_path) {
    const std::string path = sysdata_path + "aes_keys.txt";
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;

        std::string name = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();

        std::array<u8, 16> key;
        if (!parseHexKey(value, key)) continue;

        if (name == "slot0x2CKeyN") { std::copy(key.begin(), key.end(), g_slot_2c_normal_key.begin()); g_keys_loaded = true; }
        else if (name == "slot0x25KeyN") { std::copy(key.begin(), key.end(), g_slot_25_normal_key.begin()); }
        else if (name == "slot0x18KeyN") { std::copy(key.begin(), key.end(), g_slot_18_normal_key.begin()); }
        else if (name == "slot0x1BKeyN") { std::copy(key.begin(), key.end(), g_slot_1b_normal_key.begin()); }
        else if (name == "slot0x2CKeyX") { std::copy(key.begin(), key.end(), g_slot_2c_key_x.begin()); g_has_key_x_2c = true; }
        else if (name == "slot0x25KeyX") { std::copy(key.begin(), key.end(), g_slot_25_key_x.begin()); g_has_key_x_25 = true; }
        else if (name == "slot0x18KeyX") { std::copy(key.begin(), key.end(), g_slot_18_key_x.begin()); g_has_key_x_18 = true; }
        else if (name == "slot0x1BKeyX") { std::copy(key.begin(), key.end(), g_slot_1b_key_x.begin()); g_has_key_x_1b = true; }
        else if (name == "generatorConstant") { std::copy(key.begin(), key.end(), g_generator_constant.begin()); }
    }
    bool gen_zero = std::all_of(g_generator_constant.begin(), g_generator_constant.end(), [](u8 b) { return b == 0; });
    if (!g_keys_loaded && g_has_key_x_2c && !gen_zero)
        g_keys_loaded = true;
    if (g_keys_loaded && gen_zero && (g_has_key_x_2c || g_has_key_x_25 || g_has_key_x_18 || g_has_key_x_1b))
        Logger::log("NCCH: generatorConstant not set in aes_keys.txt (required for key derivation from KeyX/KeyY).\n");
    return g_keys_loaded;
}

/// SeedDB entry: 8 bytes program_id (LE) + 16 bytes seed + 8 bytes reserved = 32 bytes.
bool loadSeedFromSeedDb(const std::string& sysdata_path, u64 program_id, std::array<u8, 16>& seed_out) {
    const std::string path = sysdata_path + "seeddb.bin";
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    constexpr std::size_t kEntrySize = 32;
    u8 entry[kEntrySize];
    while (f.read(reinterpret_cast<char*>(entry), kEntrySize)) {
        u64 id = 0;
        for (int i = 0; i < 8; ++i) id |= static_cast<u64>(entry[i]) << (i * 8);
        if (id == program_id) {
            std::copy(entry + 8, entry + 24, seed_out.begin());
            return true;
        }
    }
    return false;
}

constexpr u64 kProgramIdPokemonX = 0x0004000000055D00u;

/// First 16 bytes of NCCH signature = KeyY. If seed crypto: SecondKeyY = first 16 bytes of SHA256(KeyY || seed).
void getSecondaryKeyY(const NcchHeader& ncch_header, const std::string& sysdata_path,
                      std::array<u8, 16>& key_y_primary, std::array<u8, 16>& second_key_y) {
    std::copy(ncch_header.signature, ncch_header.signature + 16, key_y_primary.begin());
    if ((ncch_header.crypto_info & kSeedCryptoFlag) == 0) {
        if (ncch_header.program_id == kProgramIdPokemonX)
            Logger::log("NCCH: ProgramID 0004000000055D00: no seed crypto flag; using primary KeyY (no seeddb).\n");
        second_key_y = key_y_primary;
        return;
    }
    std::array<u8, 16> seed{};
    if (!loadSeedFromSeedDb(sysdata_path, ncch_header.program_id, seed)) {
        if (ncch_header.program_id == kProgramIdPokemonX)
            Logger::log("NCCH: ProgramID 0004000000055D00: no seed in seeddb.bin; fallback to primary KeyY.\n");
        else
            Logger::log("NCCH: seed crypto set but no seed for program ID %016llX in seeddb.bin; using primary KeyY.\n",
                       static_cast<unsigned long long>(ncch_header.program_id));
        second_key_y = key_y_primary;
        return;
    }
    if (ncch_header.program_id == kProgramIdPokemonX)
        Logger::log("NCCH: ProgramID 0004000000055D00: seed found in seeddb.bin; using SecondKeyY (SHA256(KeyY||seed)).\n");
    u8 temp[32];
    std::memcpy(temp, key_y_primary.data(), 16);
    std::memcpy(temp + 16, seed.data(), 16);
    u8 sha_out[32];
    Sha256::hash(temp, 32, sha_out);
    std::copy(sha_out, sha_out + 16, second_key_y.begin());
}

static bool hasKeyX(std::size_t slot_id) {
    switch (slot_id) {
        case kNCCHSecure1: return g_has_key_x_2c;
        case kNCCHSecure2: return g_has_key_x_25;
        case kNCCHSecure3: return g_has_key_x_18;
        case kNCCHSecure4: return g_has_key_x_1b;
        default: return false;
    }
}

static const std::array<u8, 16>* getKeyX(std::size_t slot_id) {
    switch (slot_id) {
        case kNCCHSecure1: return &g_slot_2c_key_x;
        case kNCCHSecure2: return &g_slot_25_key_x;
        case kNCCHSecure3: return &g_slot_18_key_x;
        case kNCCHSecure4: return &g_slot_1b_key_x;
        default: return nullptr;
    }
}

/// Get primary NormalKey (slot 0x2C): derive from KeyX+KeyY+generator if available, else use pre-derived KeyN.
void getOrDerivePrimaryKey(const std::array<u8, 16>& key_y_primary, std::array<u8, 16>& out) {
    if (g_has_key_x_2c && !std::all_of(g_generator_constant.begin(), g_generator_constant.end(), [](u8 b) { return b == 0; })) {
        Aes::AESKey key_x, gen, key_y;
        std::copy(g_slot_2c_key_x.begin(), g_slot_2c_key_x.end(), key_x.begin());
        std::copy(g_generator_constant.begin(), g_generator_constant.end(), gen.begin());
        std::copy(key_y_primary.begin(), key_y_primary.end(), key_y.begin());
        Aes::AESKey derived = Aes::deriveNormalKey(key_x, key_y, gen);
        std::copy(derived.begin(), derived.end(), out.begin());
    } else {
        std::copy(g_slot_2c_normal_key.begin(), g_slot_2c_normal_key.end(), out.begin());
    }
}

void getOrDeriveSecondaryKey(std::size_t slot_id, const std::array<u8, 16>& second_key_y, std::array<u8, 16>& out) {
    if (hasKeyX(slot_id) && getKeyX(slot_id) && !std::all_of(g_generator_constant.begin(), g_generator_constant.end(), [](u8 b) { return b == 0; })) {
        Aes::AESKey key_x, gen, key_y;
        std::copy(getKeyX(slot_id)->begin(), getKeyX(slot_id)->end(), key_x.begin());
        std::copy(g_generator_constant.begin(), g_generator_constant.end(), gen.begin());
        std::copy(second_key_y.begin(), second_key_y.end(), key_y.begin());
        Aes::AESKey derived = Aes::deriveNormalKey(key_x, key_y, gen);
        std::copy(derived.begin(), derived.end(), out.begin());
    } else {
        const u8* pre = getNormalKey(slot_id);
        std::copy(pre, pre + 16, out.begin());
    }
}

bool tryDecryptExeFsCode(const std::string& filepath, const std::string& sysdata_path,
                         u32 ncch_offset, u32 partition,
                         const u8* partition_id, u8 secondary_key_slot,
                         std::vector<u8>& out_code) {
    if (!g_keys_loaded) {
        Logger::log("NCCH: Encrypted ROM detected. No aes_keys.txt found.\n");
        Logger::log("      Create sysdata/aes_keys.txt with slot0x2CKeyN or slot0x2CKeyX+generatorConstant\n");
        Logger::log("      Or use GodMode9/ctrtool to decrypt the ROM first.\n");
        return false;
    }

    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    constexpr u32 MAGIC_NCSD = 0x4453434E;
    constexpr u32 MAGIC_NCCH = 0x4843434E;

    NcchHeader ncch_header{};
    file.seekg(ncch_offset, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(&ncch_header), sizeof(NcchHeader))) return false;

    if (ncch_header.magic == MAGIC_NCSD) {
        NcsdHeader ncsd{};
        file.seekg(ncch_offset, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&ncsd), sizeof(NcsdHeader))) return false;
        if (partition >= 8 || ncsd.partitions[partition].offset == 0) return false;
        ncch_offset = ncsd.partitions[partition].offset * kBlockSize;
        file.seekg(ncch_offset, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&ncch_header), sizeof(NcchHeader))) return false;
    }

    if (ncch_header.magic != MAGIC_NCCH || ncch_header.is_no_crypto()) return false;
    if (ncch_header.exefs_size == 0 || ncch_header.exefs_offset == 0) return false;

    std::array<u8, 16> key_y_primary, second_key_y;
    getSecondaryKeyY(ncch_header, sysdata_path, key_y_primary, second_key_y);

    std::array<u8, 16> primary_key_buf, secondary_key_buf;
    getOrDerivePrimaryKey(key_y_primary, primary_key_buf);
    const std::size_t sec_slot = slotFromSecondary(secondary_key_slot);
    getOrDeriveSecondaryKey(sec_slot, second_key_y, secondary_key_buf);
    const u8* primary_key = primary_key_buf.data();
    const u8* secondary_key = secondary_key_buf.data();

    const u32 exefs_off = ncch_header.exefs_offset * kBlockSize;
    const std::size_t exefs_header_pos = ncch_offset + exefs_off;

    // Decrypt ExeFS header
    std::array<u8, kExeFsHeaderSize> exefs_header_enc{};
    file.seekg(exefs_header_pos, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(exefs_header_enc.data()), kExeFsHeaderSize)) return false;

    std::array<u8, 16> ctr;
    buildSectionCtr(partition_id, 2, ctr);
    decryptCtr(primary_key, ctr, exefs_header_enc.data(), kExeFsHeaderSize, 0);

    ExeFsHeader exefs_header{};
    std::memcpy(&exefs_header, exefs_header_enc.data(), sizeof(ExeFsHeader));

    // Find .code section
    u32 code_offset = 0, code_size = 0;
    for (u32 i = 0; i < kMaxSections; ++i) {
        if (std::strncmp(exefs_header.section[i].name, ".code", 8) == 0) {
            code_offset = exefs_header.section[i].offset;
            code_size = exefs_header.section[i].size;
            break;
        }
    }
    if (code_size == 0) return false;

    // ExeFS .code offset within ExeFS: 0x200 + section.offset. CTR block = (0x200 + offset) / 16
    const u64 code_block_offset = (kExeFsHeaderSize + code_offset) / 16;

    out_code.resize(code_size);
    file.seekg(exefs_header_pos + kExeFsHeaderSize + code_offset, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(out_code.data()), code_size)) return false;

    buildSectionCtr(partition_id, 2, ctr);
    decryptCtr(secondary_key, ctr, out_code.data(), code_size, code_block_offset);

    return true;
}

bool tryDecryptExheader(const std::string& filepath, const std::string& sysdata_path,
                        u32 ncch_offset, u32 partition,
                        const u8* partition_id, ExHeader& out_exheader) {
    if (!g_keys_loaded) return false;

    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    constexpr u32 MAGIC_NCSD = 0x4453434E;
    constexpr u32 MAGIC_NCCH = 0x4843434E;

    NcchHeader ncch_header{};
    file.seekg(ncch_offset, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(&ncch_header), sizeof(NcchHeader))) return false;

    if (ncch_header.magic == MAGIC_NCSD) {
        NcsdHeader ncsd{};
        file.seekg(ncch_offset, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&ncsd), sizeof(NcsdHeader))) return false;
        if (partition >= 8 || ncsd.partitions[partition].offset == 0) return false;
        ncch_offset = ncsd.partitions[partition].offset * kBlockSize;
        file.seekg(ncch_offset, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(&ncch_header), sizeof(NcchHeader))) return false;
    }

    if (ncch_header.magic != MAGIC_NCCH || ncch_header.is_no_crypto()) return false;
    if (ncch_header.extended_header_size == 0) return false;

    std::array<u8, 16> key_y_primary;
    std::copy(ncch_header.signature, ncch_header.signature + 16, key_y_primary.begin());
    std::array<u8, 16> primary_key_buf;
    getOrDerivePrimaryKey(key_y_primary, primary_key_buf);
    const u8* primary_key = primary_key_buf.data();
    const std::size_t exheader_size = sizeof(ExHeader);
    const std::size_t exheader_pos = ncch_offset + 0x200;

    std::vector<u8> exheader_enc(exheader_size);
    file.seekg(exheader_pos, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(exheader_enc.data()), exheader_size)) return false;

    std::array<u8, 16> ctr;
    buildSectionCtr(partition_id, 1, ctr);  // type 1 = Exheader
    decryptCtr(primary_key, ctr, exheader_enc.data(), exheader_size, 0);

    std::memcpy(&out_exheader, exheader_enc.data(), exheader_size);
    return true;
}
