#include "ncch.hpp"
#include "../../common/logger.hpp"
#include <cstring>

namespace {

constexpr u32 MAGIC_NCCH = 0x4843434E;  // "NCCH"
constexpr u32 MAGIC_NCSD = 0x4453434E;  // "NCSD"

inline u32 readLe32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

// 3DS ExeFS `.code` reverse-LZSS decompression.
// Matches the algorithm used by ctrtool (`--decompresscode`).
// Footer (last 8 bytes):
// - u32 buffertopandbottom
// - u32 originalbottom
bool decompressExeFsCodeReverseLzss(const std::vector<u8>& compressed, std::vector<u8>& decompressed) {
    if (compressed.size() < 8) return false;

    const std::size_t compressed_size = compressed.size();
    const u8* footer = compressed.data() + compressed_size - 8;
    const u32 buffertopandbottom = readLe32(footer + 0);
    const u32 originalbottom = readLe32(footer + 4);

    const std::size_t decompressed_size = static_cast<std::size_t>(originalbottom) + compressed_size;
    if (decompressed_size <= compressed_size) return false;
    if (decompressed_size > (128u * 1024u * 1024u)) return false;  // defensive cap

    decompressed.assign(decompressed_size, 0);
    std::memcpy(decompressed.data(), compressed.data(), compressed_size);

    std::size_t out = decompressed_size;
    std::size_t index = compressed_size - ((buffertopandbottom >> 24) & 0xFFu);
    const std::size_t stop_index = compressed_size - (buffertopandbottom & 0x00FFFFFFu);

    if (index > compressed_size || stop_index > compressed_size) return false;
    if (stop_index > index) return false;

    while (index > stop_index) {
        const u8 control = compressed[--index];
        u8 ctrl = control;

        for (u32 i = 0; i < 8; ++i) {
            if (index <= stop_index || index == 0 || out == 0) break;

            if (ctrl & 0x80u) {
                if (index < 2) return false;
                index -= 2;

                u32 seg = static_cast<u32>(compressed[index]) | (static_cast<u32>(compressed[index + 1]) << 8);
                const std::size_t seg_size = static_cast<std::size_t>(((seg >> 12) & 0xFu) + 3u);
                std::size_t seg_off = static_cast<std::size_t>(seg & 0x0FFFu);
                seg_off += 2;

                if (out < seg_size) return false;
                for (std::size_t j = 0; j < seg_size; ++j) {
                    if (out + seg_off >= decompressed_size) return false;
                    const u8 data = decompressed[out + seg_off];
                    decompressed[--out] = data;
                }
            } else {
                if (index == 0 || out == 0) return false;
                decompressed[--out] = compressed[--index];
            }
            ctrl <<= 1;
        }
    }

    // Most blobs fully fill the output (out == 0). If it doesn't, it's still useful for debugging.
    return true;
}

}  // namespace

NcchResult NcchContainer::open(const std::string& path, u32 offset, u32 part) {
    filepath_ = path;
    ncch_offset_ = offset;
    partition_ = part;
    file_.open(filepath_, std::ios::binary);
    if (!file_) {
        return NcchResult::Error;
    }
    file_.seekg(ncch_offset_, std::ios::beg);
    if (!file_.read(reinterpret_cast<char*>(&ncch_header_), sizeof(NcchHeader))) {
        return NcchResult::Error;
    }

    block_size_ = kBlockSize;
    if (ncch_header_.magic == MAGIC_NCSD) {
        is_ncsd_ = true;
        NcsdHeader ncsd{};
        file_.seekg(ncch_offset_, std::ios::beg);
        if (!file_.read(reinterpret_cast<char*>(&ncsd), sizeof(NcsdHeader))) {
            return NcchResult::Error;
        }
        if (partition_ >= 8 || ncsd.partitions[partition_].offset == 0) {
            return NcchResult::Error;
        }
        ncch_offset_ = ncsd.partitions[partition_].offset * kBlockSize;
        file_.seekg(ncch_offset_, std::ios::beg);
        if (!file_.read(reinterpret_cast<char*>(&ncch_header_), sizeof(NcchHeader))) {
            return NcchResult::Error;
        }
    }

    if (ncch_header_.magic != MAGIC_NCCH) {
        return NcchResult::ErrorInvalidFormat;
    }
    if (!ncch_header_.is_no_crypto()) {
        encrypted_ = true;
        has_header_ = true;
        return NcchResult::ErrorEncrypted;
    }

    has_header_ = true;
    return NcchResult::Success;
}

NcchResult NcchContainer::load() {
    if (!has_header_) {
        return NcchResult::Error;
    }

    std::size_t file_size = 0;
    file_.seekg(0, std::ios::end);
    file_size = static_cast<std::size_t>(file_.tellg());
    file_.seekg(ncch_offset_, std::ios::beg);

    if (ncch_header_.content_size != 0 &&
        static_cast<std::size_t>(ncch_header_.content_size) * block_size_ == file_size - ncch_offset_) {
        is_proto_ = true;
        block_size_ = 1;
    }

    if (ncch_header_.extended_header_size != 0 || is_proto_) {
        const std::size_t exheader_off = ncch_offset_ + 0x200;
        file_.seekg(exheader_off, std::ios::beg);
        if (!file_.read(reinterpret_cast<char*>(&exheader_), sizeof(ExHeader))) {
            return NcchResult::Error;
        }
        has_exheader_ = true;
    }

    if (ncch_header_.exefs_size != 0) {
        exefs_offset_ = ncch_header_.exefs_offset * block_size_;
        const std::size_t exefs_header_pos = ncch_offset_ + exefs_offset_;
        file_.seekg(exefs_header_pos, std::ios::beg);
        if (!file_.read(reinterpret_cast<char*>(&exefs_header_), sizeof(ExeFsHeader))) {
            return NcchResult::Error;
        }
        has_exefs_ = true;
    }

    return NcchResult::Success;
}

NcchResult NcchContainer::loadSectionCode(std::vector<u8>& buffer) {
    if (load() != NcchResult::Success) {
        return NcchResult::Error;
    }
    if (!has_exefs_) {
        return NcchResult::Error;
    }

    for (u32 i = 0; i < kMaxSections; ++i) {
        const auto& section = exefs_header_.section[i];
        if (std::strncmp(section.name, ".code", 8) == 0) {
            const std::size_t section_offset = exefs_offset_ + sizeof(ExeFsHeader) + section.offset + ncch_offset_;
            const std::size_t section_size = section.size;
            buffer.resize(section_size);
            file_.seekg(section_offset, std::ios::beg);
            if (!file_.read(reinterpret_cast<char*>(buffer.data()), section_size)) {
                return NcchResult::Error;
            }

            // If exheader is present, we know the expected in-memory size of the code blob
            // (text+ro+data, page-aligned). When `.code` is compressed, its file size is smaller.
            if (has_exheader_) {
                const std::size_t expected = static_cast<std::size_t>(exheader_.codeset_info.text.code_size) +
                                             static_cast<std::size_t>(exheader_.codeset_info.ro.code_size) +
                                             static_cast<std::size_t>(exheader_.codeset_info.data.code_size);
                if (expected > 0 && buffer.size() != expected && buffer.size() < expected) {
                    std::vector<u8> dec;
                    if (decompressExeFsCodeReverseLzss(buffer, dec) && dec.size() == expected) {
                        Logger::log("NCCH: decompressed ExeFS .code (reverse-LZSS) %zu -> %zu bytes\n",
                                    buffer.size(), dec.size());
                        buffer.swap(dec);
                    } else {
                        Logger::log("NCCH: WARNING: .code smaller than expected (%zu < %zu) but decompression failed\n",
                                    buffer.size(), expected);
                    }
                }
            }

            return NcchResult::Success;
        }
    }
    return NcchResult::ErrorNotUsed;
}
