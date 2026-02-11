#include "loader.hpp"
#include "../file_sys/ncch.hpp"
#include "../file_sys/ncch_decrypt.hpp"
#include "../../common/logger.hpp"
#include <cstdio>
#include <fstream>
#include <cstring>
#include <string>

namespace {

constexpr u32 MAGIC_NCCH = 0x4843434E;  // "NCCH"
constexpr u32 MAGIC_NCSD = 0x4453434E;  // "NCSD"

/// Log first 16 bytes of .code to detect encrypted vs decrypted (ARM entry often starts 06 00 00 EA = B).
void logCodeHead(const char* source, const std::vector<u8>& code) {
    const std::size_t n = std::min(code.size(), std::size_t{16});
    char buf[64];
    std::snprintf(buf, sizeof(buf), "Loader: code source = %s. First %zu bytes: ", source, n);
    std::string line = buf;
    for (std::size_t i = 0; i < n; ++i) {
        char b[4];
        std::snprintf(b, sizeof(b), "%02X ", code[i]);
        line += b;
    }
    Logger::log("%s\n", line.c_str());
    if (n >= 4 && code[0] == 0x06 && code[1] == 0x00 && code[2] == 0x00 && code[3] == 0xEA)
        Logger::log("Loader: looks like valid ARM entry (06 00 00 EA = B).\n");
    else if (n >= 4)
        Logger::log("Loader: WARNING: first word is not typical ARM B; may be encrypted or wrong offset.\n");
}

std::string getSysdataPath() {
    const char* paths[] = {"sysdata/", "sysdata\\", ""};
    for (const char* p : paths) {
        std::ifstream f(std::string(p) + "aes_keys.txt");
        if (f) return p;
    }
    return "";
}

}  // namespace

LoaderFileType identifyFile(const std::string& filepath) {
    std::ifstream f(filepath, std::ios::binary);
    if (!f) {
        return LoaderFileType::Error;
    }
    u32 magic = 0;
    f.seekg(0x100, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(&magic), sizeof(magic))) {
        return LoaderFileType::Error;
    }
    if (magic == MAGIC_NCSD) {
        return LoaderFileType::CCI;
    }
    if (magic == MAGIC_NCCH) {
        return LoaderFileType::CXI;
    }
    return LoaderFileType::Unknown;
}

LoaderResult loadNcch(const std::string& filepath, LoadResult& out) {
    LoaderFileType type = identifyFile(filepath);
    if (type != LoaderFileType::CXI && type != LoaderFileType::CCI) {
        return LoaderResult::ErrorInvalidFormat;
    }

    NcchContainer ncch;
    NcchResult open_result = ncch.open(filepath, 0, 0);

    if (open_result == NcchResult::ErrorEncrypted && ncch.isEncrypted()) {
        const std::string sysdata = getSysdataPath();
        if (loadAesKeys(sysdata)) {
            ExHeader exheader{};
            if (tryDecryptExheader(filepath, sysdata, ncch.getNcchOffset(), ncch.getPartition(),
                                  ncch.getPartitionId(), exheader) &&
                tryDecryptExeFsCode(filepath, sysdata, ncch.getNcchOffset(), ncch.getPartition(),
                                   ncch.getPartitionId(), ncch.getSecondaryKeySlot(), out.code)) {
                out.entry_point = exheader.codeset_info.text.address;
                out.text_address = exheader.codeset_info.text.address;
                out.text_size = exheader.codeset_info.text.code_size;
                out.stack_size = exheader.codeset_info.stack_size;
                out.program_id = ncch.getProgramId();

                const u32 ro_addr = exheader.codeset_info.ro.address;
                const u32 ro_size = exheader.codeset_info.ro.code_size;
                const u32 data_addr = exheader.codeset_info.data.address;
                const u32 data_size = exheader.codeset_info.data.code_size;
                const u32 bss_size = exheader.codeset_info.bss_size;

                const std::size_t min_code_size = out.text_size + ro_size + data_size;
                if (min_code_size > 0 && min_code_size <= out.code.size()) {
                    std::size_t offset = 0;
                    if (out.text_size > 0) {
                        CodeSegment seg;
                        seg.vaddr = out.text_address;
                        seg.size = out.text_size;
                        seg.data.assign(out.code.begin() + offset, out.code.begin() + offset + out.text_size);
                        out.segments.push_back(std::move(seg));
                        offset += out.text_size;
                    }
                    if (ro_size > 0) {
                        CodeSegment seg;
                        seg.vaddr = ro_addr;
                        seg.size = ro_size;
                        seg.data.assign(out.code.begin() + offset, out.code.begin() + offset + ro_size);
                        out.segments.push_back(std::move(seg));
                        offset += ro_size;
                    }
                    if (data_size > 0) {
                        CodeSegment seg;
                        const std::size_t bss_pages = (bss_size + 0xFFF) & ~0xFFFu;
                        seg.vaddr = data_addr;
                        seg.size = static_cast<u32>(data_size + bss_pages);
                        seg.data.resize(data_size + bss_pages, 0);
                        std::memcpy(seg.data.data(), out.code.data() + offset, data_size);
                        out.segments.push_back(std::move(seg));
                    }
                    if (!out.segments.empty()) {
                        out.use_segment_mapping = true;
                    }
                }
                logCodeHead("decrypted (AES)", out.code);
                if (out.code.size() >= 4 && (out.code[0] != 0x06 || out.code[1] != 0x00 || out.code[2] != 0x00 || out.code[3] != 0xEA))
                    Logger::log("Loader: ALARM: decrypted code does not look like valid ARM (expected branch 06 00 00 EA).\n");
                return LoaderResult::Success;
            }
        }
        return LoaderResult::ErrorEncrypted;
    }
    if (open_result != NcchResult::Success) {
        return LoaderResult::ErrorEncrypted;
    }

    if (ncch.load() != NcchResult::Success) {
        return LoaderResult::ErrorNotLoaded;
    }

    if (ncch.loadSectionCode(out.code) != NcchResult::Success) {
        return LoaderResult::ErrorNotUsed;
    }

    out.entry_point = ncch.getEntryPoint();
    out.text_address = ncch.getTextAddress();
    out.text_size = ncch.getTextSize();
    out.stack_size = ncch.getStackSize();
    out.program_id = ncch.getProgramId();

    const u32 ro_addr = ncch.getRoAddress();
    const u32 ro_size = ncch.getRoSize();
    const u32 data_addr = ncch.getDataAddress();
    const u32 data_size = ncch.getDataSize();
    const u32 bss_size = ncch.getBssSize();

    const std::size_t min_code_size = out.text_size + ro_size + data_size;
    if (min_code_size > 0 && min_code_size <= out.code.size()) {
        std::size_t offset = 0;
        if (out.text_size > 0) {
            CodeSegment seg;
            seg.vaddr = out.text_address;
            seg.size = out.text_size;
            seg.data.assign(out.code.begin() + offset, out.code.begin() + offset + out.text_size);
            out.segments.push_back(std::move(seg));
            offset += out.text_size;
        }
        if (ro_size > 0) {
            CodeSegment seg;
            seg.vaddr = ro_addr;
            seg.size = ro_size;
            seg.data.assign(out.code.begin() + offset, out.code.begin() + offset + ro_size);
            out.segments.push_back(std::move(seg));
            offset += ro_size;
        }
        if (data_size > 0) {
            CodeSegment seg;
            const std::size_t bss_pages = (bss_size + 0xFFF) & ~0xFFFu;
            seg.vaddr = data_addr;
            seg.size = static_cast<u32>(data_size + bss_pages);
            seg.data.resize(data_size + bss_pages, 0);
            std::memcpy(seg.data.data(), out.code.data() + offset, data_size);
            out.segments.push_back(std::move(seg));
        }
        if (!out.segments.empty()) {
            out.use_segment_mapping = true;
        }
    }

    const bool looks_arm = (out.code.size() >= 4 && out.code[0] == 0x06 && out.code[1] == 0x00 && out.code[2] == 0x00 && out.code[3] == 0xEA);
    if (!looks_arm) {
        const std::string sysdata = getSysdataPath();
        if (loadAesKeys(sysdata)) {
            ExHeader exheader{};
            if (tryDecryptExheader(filepath, sysdata, ncch.getNcchOffset(), ncch.getPartition(),
                                  ncch.getPartitionId(), exheader) &&
                tryDecryptExeFsCode(filepath, sysdata, ncch.getNcchOffset(), ncch.getPartition(),
                                   ncch.getPartitionId(), ncch.getSecondaryKeySlot(), out.code)) {
                out.entry_point = exheader.codeset_info.text.address;
                out.text_address = exheader.codeset_info.text.address;
                out.text_size = exheader.codeset_info.text.code_size;
                out.stack_size = exheader.codeset_info.stack_size;
                out.program_id = ncch.getProgramId();
                const u32 ro_addr = exheader.codeset_info.ro.address;
                const u32 ro_size = exheader.codeset_info.ro.code_size;
                const u32 data_addr = exheader.codeset_info.data.address;
                const u32 data_size = exheader.codeset_info.data.code_size;
                const u32 bss_size = exheader.codeset_info.bss_size;
                const std::size_t min_code_size = out.text_size + ro_size + data_size;
                out.segments.clear();
                if (min_code_size > 0 && min_code_size <= out.code.size()) {
                    std::size_t offset = 0;
                    if (out.text_size > 0) {
                        CodeSegment seg;
                        seg.vaddr = out.text_address;
                        seg.size = out.text_size;
                        seg.data.assign(out.code.begin() + offset, out.code.begin() + offset + out.text_size);
                        out.segments.push_back(std::move(seg));
                        offset += out.text_size;
                    }
                    if (ro_size > 0) {
                        CodeSegment seg;
                        seg.vaddr = ro_addr;
                        seg.size = ro_size;
                        seg.data.assign(out.code.begin() + offset, out.code.begin() + offset + ro_size);
                        out.segments.push_back(std::move(seg));
                        offset += ro_size;
                    }
                    if (data_size > 0) {
                        CodeSegment seg;
                        const std::size_t bss_pages = (bss_size + 0xFFF) & ~0xFFFu;
                        seg.vaddr = data_addr;
                        seg.size = static_cast<u32>(data_size + bss_pages);
                        seg.data.resize(data_size + bss_pages, 0);
                        std::memcpy(seg.data.data(), out.code.data() + offset, data_size);
                        out.segments.push_back(std::move(seg));
                    }
                    if (!out.segments.empty())
                        out.use_segment_mapping = true;
                }
                logCodeHead("decrypted (AES, retry after raw)", out.code);
                if (out.code.size() >= 4 && (out.code[0] != 0x06 || out.code[1] != 0x00 || out.code[2] != 0x00 || out.code[3] != 0xEA))
                    Logger::log("Loader: ALARM: decrypted code does not look like valid ARM (expected branch 06 00 00 EA).\n");
                return LoaderResult::Success;
            }
        }
    }

    logCodeHead("raw (no crypto)", out.code);
    return LoaderResult::Success;
}
