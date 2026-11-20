#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace openra_recomp {

using SectorReadFn = std::function<bool(uint32_t lba, uint8_t* out2048)>;
using ProgressFn = std::function<void(int percent, const std::string& msg)>;

struct IsoExtractResult {
    bool ok = false;
    int filesExtracted = 0;
    uint64_t bytesExtracted = 0;
    std::string error;
};

inline uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline std::string sanitizeIsoName(const char* raw, uint8_t len) {
    std::string name(raw, raw + len);
    auto semi = name.find(';');
    if (semi != std::string::npos) {
        name.resize(semi);
    }
    while (!name.empty() && name.back() == '.') {
        name.pop_back();
    }
    for (char& c : name) {
        if (c == '/' || c == '\\') c = '_';
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return name;
}

inline bool isRelevantOpenRaAsset(const std::string& lowerName) {
    static const char* kExts[] = {
        ".mix", ".aud", ".vqa", ".shp", ".tmp", ".pal",
        ".cps", ".wsa", ".pak", ".ini", ".bin", ".fnt", ".mng"
    };
    for (const char* ext : kExts) {
        size_t elen = std::strlen(ext);
        if (lowerName.size() >= elen &&
            lowerName.compare(lowerName.size() - elen, elen, ext) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace openra_recomp
