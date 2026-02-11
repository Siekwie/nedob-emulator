#pragma once

#include "../../common/common_types.hpp"
#include "ncch.hpp"
#include <string>
#include <vector>

/// Decrypt ExHeader into out_exheader. sysdata_path used for seeddb.bin if seed crypto.
bool tryDecryptExheader(const std::string& filepath, const std::string& sysdata_path,
                        u32 ncch_offset, u32 partition,
                        const u8* partition_id, ExHeader& out_exheader);

/// NCCH decryption for encrypted ExeFS/.code.
/// Requires aes_keys.txt: slot0x2CKeyN or slot0x2CKeyX+generatorConstant; KeyY from NCCH signature; seeddb.bin for 7.x seed.
bool tryDecryptExeFsCode(const std::string& filepath, const std::string& sysdata_path,
                        u32 ncch_offset, u32 partition,
                        const u8* partition_id, u8 secondary_key_slot,
                        std::vector<u8>& out_code);

/// Load aes_keys from sysdata/aes_keys.txt. Format: slot0x2CKeyN=0123... (32 hex chars).
bool loadAesKeys(const std::string& sysdata_path);
