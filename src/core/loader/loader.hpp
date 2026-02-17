#pragma once

#include "../../common/common_types.hpp"
#include <memory>
#include <string>
#include <vector>

class NcchContainer;

/// File types supported for loading
enum class LoaderFileType {
    Error,
    Unknown,
    CCI,
    CXI,
};

/// Result status for loader operations
enum class LoaderResult {
    Success,
    Error,
    ErrorInvalidFormat,
    ErrorEncrypted,
    ErrorNotLoaded,
    ErrorNotUsed,
};

/// One segment (text, ro, or data) to map
struct CodeSegment {
    VAddr vaddr{0};
    u32 size{0};
    std::vector<u8> data;
};

/// Output from loading a ROM: segments and metadata
struct LoadResult {
    std::vector<u8> code;  // Raw .code blob (used when segment_mapping is false)
    VAddr entry_point{0};
    VAddr text_address{0};
    u32 text_size{0};
    u32 stack_size{0};
    u64 program_id{0};

    /// When true, use segments_ for mapping; otherwise map code at text_address
    bool use_segment_mapping{false};
    std::vector<CodeSegment> segments;
};

/// Identify file type from magic at 0x100 (CXI) or 0x0 (CCI/NCSD)
LoaderFileType identifyFile(const std::string& filepath);

/// Load CXI or CCI file. Returns code, entry point, and layout info.
/// Only supports decrypted CXI/CCI.
LoaderResult loadNcch(const std::string& filepath, LoadResult& out);
