#pragma once

#include <string>
#include <string_view>

/**
 * Enumeration of supported ROM types for the emulator.
 * Each type corresponds to a different emulator core.
 */
enum class RomType {
    Unknown,    // Unknown or unsupported file type
    NDS,        // Nintendo DS ROM (.nds)
    NDSi,       // Nintendo DSi ROM (.nds, but with DSi-specific features)
    ThreeDS,    // Nintendo 3DS ROM (.3ds, .cia)
    Invalid     // Invalid or corrupted file
};

/**
 * Detects the ROM type based on file extension.
 * 
 * @param filepath The path to the ROM file
 * @return The detected RomType
 */
inline RomType detectRomType(std::string_view filepath) {
    // Find the last dot in the filename
    auto dot_pos = filepath.find_last_of('.');
    if (dot_pos == std::string_view::npos) {
        return RomType::Unknown;
    }
    
    // Extract extension and convert to lowercase for comparison
    std::string ext;
    ext.reserve(filepath.length() - dot_pos);
    for (size_t i = dot_pos + 1; i < filepath.length(); ++i) {
        char c = filepath[i];
        if (c >= 'A' && c <= 'Z') {
            ext += static_cast<char>(c + 32); // Convert to lowercase
        } else {
            ext += c;
        }
    }
    
    // Match extension to ROM type
    if (ext == "nds") {
        return RomType::NDS;
    } else if (ext == "3ds") {
        return RomType::ThreeDS;
    } else if (ext == "cia") {
        return RomType::ThreeDS;
    }
    
    return RomType::Unknown;
}

/**
 * Gets a human-readable string for a ROM type.
 * 
 * @param type The ROM type
 * @return A string describing the ROM type
 */
inline const char* romTypeToString(RomType type) {
    switch (type) {
        case RomType::NDS:
            return "Nintendo DS";
        case RomType::NDSi:
            return "Nintendo DSi";
        case RomType::ThreeDS:
            return "Nintendo 3DS";
        case RomType::Unknown:
            return "Unknown";
        case RomType::Invalid:
            return "Invalid";
    }
    return "Unknown";
}
