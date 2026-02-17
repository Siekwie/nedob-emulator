#include "ncch.hpp"
#include "../../common/logger.hpp"
#include <cstring>

namespace {

constexpr u32 MAGIC_NCCH = 0x4843434E;  // "NCCH"
constexpr u32 MAGIC_NCSD = 0x4453434E;  // "NCSD"

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
            return NcchResult::Success;
        }
    }
    return NcchResult::ErrorNotUsed;
}
