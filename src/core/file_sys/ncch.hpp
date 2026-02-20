#pragma once

#include "../../common/common_types.hpp"
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>

/// Result of NCCH operations
enum class NcchResult {
    Success,
    Error,
    ErrorInvalidFormat,
    ErrorEncrypted,
    ErrorNotUsed,
};

/// NCCH header (first 0x200 bytes of NCCH)
struct NcchHeader {
    u8 signature[0x100];
    u32 magic;
    u32 content_size;
    u8 partition_id[8];
    u16 maker_code;
    u16 version;
    u8 reserved_0[4];
    u64 program_id;
    u8 reserved_1[0x10];
    u8 logo_region_hash[0x20];
    u8 product_code[0x10];
    u8 extended_header_hash[0x20];
    u32 extended_header_size;
    u8 reserved_2[4];
    u8 reserved_flag[3];
    u8 secondary_key_slot;
    u8 platform;
    u8 content_info;  // is_data, is_executable, content_type
    u8 content_unit_size;
    u8 crypto_info;  // fixed_key, no_romfs, no_crypto, etc.
    u32 plain_region_offset;
    u32 plain_region_size;
    u32 logo_region_offset;
    u32 logo_region_size;
    u32 exefs_offset;
    u32 exefs_size;
    u32 exefs_hash_region_size;
    u8 reserved_3[4];
    u32 romfs_offset;
    u32 romfs_size;
    u32 romfs_hash_region_size;
    u8 reserved_4[4];
    u8 exefs_super_block_hash[0x20];
    u8 romfs_super_block_hash[0x20];

    bool is_no_crypto() const { return (crypto_info & 4) != 0; }
    u32 get_content_unit_size() const { return 0x200u * (1u << (content_unit_size & 0x1F)); }
};

/// ExeFS section header (8-byte name + offset + size)
struct ExeFsSectionHeader {
    char name[8];
    u32 offset;
    u32 size;
};

/// ExeFS header
struct ExeFsHeader {
    ExeFsSectionHeader section[8];
    u8 reserved[0x80];
    u8 hashes[8][0x20];
};

/// ExHeader code segment info
struct ExHeaderCodeSegmentInfo {
    u32 address;
    u32 num_max_pages;
    u32 code_size;
};

/// ExHeader code set info (name, text, ro, data, stack, bss)
struct ExHeaderCodeSetInfo {
    u8 name[8];
    u8 flags_reserved[8];
    ExHeaderCodeSegmentInfo text;
    u32 stack_size;
    ExHeaderCodeSegmentInfo ro;
    u8 reserved[4];
    ExHeaderCodeSegmentInfo data;
    u32 bss_size;
};

/// ExHeader (first part we need - 0x800 bytes)
struct ExHeader {
    ExHeaderCodeSetInfo codeset_info;
    u8 dependency_list[0x30 * 8];
    u8 system_info[0x40];
    u8 arm11_system_local_caps[0xE8];
    u8 arm11_kernel_caps[0x80];
    u8 arm9_access_control[0x100];
    u8 access_desc[0x200];
};

/// NCSD (CCI) header - for partition table
struct NcsdHeader {
    u8 signature[0x100];
    u32 magic;
    u32 media_size;
    u8 media_id[8];
    u8 partition_fs_type[8];
    u8 partition_crypt_type[8];
    struct {
        u32 offset;
        u32 size;
    } partitions[8];
    u8 extended_header_hash[0x20];
    u32 additional_header_size;
    u32 sector_zero_offset;
    u8 partition_flags[8];
    u8 partition_id_table[0x40];
    u8 reserved[0x30];
};

/// NCCH container parser - parses CXI or first NCCH from CCI.
/// Supports decrypted (no_crypto) NCCH. Encrypted NCCH requires aes_keys.txt (see ncch_decrypt).
class NcchContainer {
public:
    NcchContainer() = default;

    /// Open and parse NCCH from file. For CCI, uses partition 0 (main game).
    NcchResult open(const std::string& filepath, u32 ncch_offset = 0, u32 partition = 0);

    /// Load full NCCH (header + exheader + exefs).
    NcchResult load();

    /// Load .code section from ExeFS into buffer.
    NcchResult loadSectionCode(std::vector<u8>& buffer);

    /// Get ro segment address.
    u32 getRoAddress() const { return exheader_.codeset_info.ro.address; }
    /// Get ro segment size (code_size from ExHeader).
    u32 getRoSize() const { return exheader_.codeset_info.ro.code_size; }
    /// Get data segment address.
    u32 getDataAddress() const { return exheader_.codeset_info.data.address; }
    /// Get data segment size (code_size from ExHeader).
    u32 getDataSize() const { return exheader_.codeset_info.data.code_size; }
    /// Get bss size for zero-initialization.
    u32 getBssSize() const { return exheader_.codeset_info.bss_size; }

    /// Get entry point (text segment address from ExHeader).
    u32 getEntryPoint() const { return exheader_.codeset_info.text.address; }

    /// Get stack size from ExHeader.
    u32 getStackSize() const { return exheader_.codeset_info.stack_size; }

    /// Get text (code) segment address.
    u32 getTextAddress() const { return exheader_.codeset_info.text.address; }

    /// Get text segment size (code_size).
    u32 getTextSize() const { return exheader_.codeset_info.text.code_size; }

    /// Get max pages for each code segment (page size 0x1000).
    u32 getTextMaxPages() const { return exheader_.codeset_info.text.num_max_pages; }
    u32 getRoMaxPages() const { return exheader_.codeset_info.ro.num_max_pages; }
    u32 getDataMaxPages() const { return exheader_.codeset_info.data.num_max_pages; }

    /// Get program ID.
    u64 getProgramId() const { return ncch_header_.program_id; }

    bool hasExeFs() const { return has_exefs_; }
    bool hasExHeader() const { return has_exheader_; }
    bool isEncrypted() const { return encrypted_; }
    const u8* getPartitionId() const { return ncch_header_.partition_id; }
    u8 getSecondaryKeySlot() const { return ncch_header_.secondary_key_slot; }
    u32 getNcchOffset() const { return ncch_offset_; }
    u32 getPartition() const { return partition_; }

private:
    static constexpr u32 kBlockSize = 0x200;
    static constexpr u32 kMaxSections = 8;
    static constexpr u32 MakeMagic(char a, char b, char c, char d) {
        return static_cast<u32>(a) | (static_cast<u32>(b) << 8) | (static_cast<u32>(c) << 16) |
               (static_cast<u32>(d) << 24);
    }

    std::string filepath_;
    u32 ncch_offset_{0};
    u32 partition_{0};
    u32 exefs_offset_{0};
    u32 block_size_{kBlockSize};
    bool is_ncsd_{false};
    bool is_proto_{false};
    bool has_header_{false};
    bool has_exheader_{false};
    bool has_exefs_{false};
    bool encrypted_{false};

    NcchHeader ncch_header_;
    ExHeader exheader_;
    ExeFsHeader exefs_header_;

    std::ifstream file_;

    NcchResult readLe32(u32& out);
    NcchResult readLe16(u16& out);
    NcchResult readLe64(u64& out);
};
