#include <jni.h>
#include <android/log.h>
#include <zlib.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef HAS_LIBCHDR
#include <libchdr/chd.h>
#endif

#include "iso9660_reader.h"
#include "sha256.h"

#define LOG_TAG "OpenRALauncherJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace fs = std::filesystem;
using namespace openra_recomp;

namespace {

enum class ArchiveFormat {
    Unknown,
    Iso2048,
    Iso2352,
    Chd,
    Zip
};

bool preadExact(int fd, off64_t offset, void* dst, size_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    size_t done = 0;
    while (done < size) {
        ssize_t n = ::pread64(fd, out + done, size - done, offset + static_cast<off64_t>(done));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

ArchiveFormat detectFormat(int fd) {
    uint8_t magic[16]{};
    if (preadExact(fd, 0, magic, sizeof(magic))) {
        if (std::memcmp(magic, "MComprHD", 8) == 0) {
            return ArchiveFormat::Chd;
        }
        if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 0x03 && magic[3] == 0x04) {
            return ArchiveFormat::Zip;
        }
    }

    uint8_t pvd[6]{};
    // Standard ISO9660 Primary Volume Descriptor at sector 16 (offset 0x8000)
    if (preadExact(fd, 16LL * 2048LL + 1LL, pvd, 5) && std::memcmp(pvd, "CD001", 5) == 0) {
        return ArchiveFormat::Iso2048;
    }
    // Raw 2352-byte CD-ROM Mode1/Mode2 PVD at sector 16 (+16 or +24 header)
    if (preadExact(fd, 16LL * 2352LL + 17LL, pvd, 5) && std::memcmp(pvd, "CD001", 5) == 0) {
        return ArchiveFormat::Iso2352;
    }
    if (preadExact(fd, 16LL * 2352LL + 25LL, pvd, 5) && std::memcmp(pvd, "CD001", 5) == 0) {
        return ArchiveFormat::Iso2352;
    }
    return ArchiveFormat::Unknown;
}

bool ensureDir(const std::string& path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

bool extractIsoStream(const SectorReadFn& readSector,
                      const std::string& outDir,
                      const ProgressFn& onProgress,
                      IsoExtractResult& result) {
    uint8_t pvd[2048]{};
    if (!readSector(16, pvd) || std::memcmp(pvd + 1, "CD001", 5) != 0) {
        result.error = "Invalid ISO9660 Primary Volume Descriptor (CD001 not found)";
        return false;
    }

    const uint32_t rootLba = readLe32(pvd + 156 + 2);
    const uint32_t rootSize = readLe32(pvd + 156 + 10);
    if (rootSize == 0 || rootSize > 16U * 1024U * 1024U) {
        result.error = "Invalid ISO9660 root directory size";
        return false;
    }

    struct DirEntry {
        uint32_t lba;
        uint32_t size;
        std::string relPath;
    };

    std::vector<DirEntry> dirQueue;
    dirQueue.push_back({rootLba, rootSize, ""});

    std::vector<uint8_t> sectorBuf(2048);
    int visitedDirs = 0;

    while (!dirQueue.empty()) {
        DirEntry cur = dirQueue.back();
        dirQueue.pop_back();
        ++visitedDirs;

        uint32_t sectors = (cur.size + 2047U) / 2048U;
        std::vector<uint8_t> dirData(sectors * 2048U);
        for (uint32_t s = 0; s < sectors; ++s) {
            if (!readSector(cur.lba + s, dirData.data() + s * 2048U)) {
                result.error = "Failed reading ISO9660 directory sector";
                return false;
            }
        }

        size_t pos = 0;
        while (pos < cur.size) {
            uint8_t recLen = dirData[pos];
            if (recLen == 0) {
                pos = ((pos / 2048U) + 1U) * 2048U;
                continue;
            }
            if (pos + recLen > dirData.size() || recLen < 34) break;

            uint32_t extentLba = readLe32(&dirData[pos + 2]);
            uint32_t dataLen = readLe32(&dirData[pos + 10]);
            uint8_t flags = dirData[pos + 25];
            uint8_t nameLen = dirData[pos + 32];

            if (pos + 33 + nameLen <= dirData.size() && nameLen > 0) {
                // Skip "." (0x00) and ".." (0x01)
                if (!(nameLen == 1 && (dirData[pos + 33] == 0x00 || dirData[pos + 33] == 0x01))) {
                    std::string cleanName = sanitizeIsoName(
                        reinterpret_cast<const char*>(&dirData[pos + 33]), nameLen);
                    if (!cleanName.empty() && cleanName != "." && cleanName != "..") {
                        bool isDir = (flags & 0x02) != 0;
                        std::string nextRel = cur.relPath.empty()
                            ? cleanName
                            : (cur.relPath + "/" + cleanName);

                        if (isDir) {
                            dirQueue.push_back({extentLba, dataLen, nextRel});
                        } else if (isRelevantOpenRaAsset(cleanName)) {
                            std::string outPath = outDir + "/" + cleanName;
                            std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
                            if (!ofs) {
                                result.error = "Failed to create output file (storage full or permission denied)";
                                return false;
                            }

                            uint32_t remaining = dataLen;
                            uint32_t lba = extentLba;
                            while (remaining > 0) {
                                if (!readSector(lba++, sectorBuf.data())) {
                                    result.error = "Failed reading ISO9660 file sector";
                                    return false;
                                }
                                uint32_t chunk = std::min<uint32_t>(remaining, 2048U);
                                ofs.write(reinterpret_cast<const char*>(sectorBuf.data()), chunk);
                                if (!ofs) {
                                    result.error = "Disk write failed (insufficient storage space)";
                                    return false;
                                }
                                remaining -= chunk;
                            }

                            result.filesExtracted++;
                            result.bytesExtracted += dataLen;
                            onProgress(std::min(90, 20 + result.filesExtracted * 3),
                                       "Extracted " + cleanName);
                        }
                    }
                }
            }
            pos += recLen;
        }
    }

    result.ok = (result.filesExtracted > 0);
    if (!result.ok && result.error.empty()) {
        result.error = "No OpenRA game assets (.mix/.aud/.vqa/.pak) found inside ISO";
    }
    return result.ok;
}

bool extractZipFd(int fd,
                  off64_t fileSize,
                  const std::string& outDir,
                  const ProgressFn& onProgress,
                  IsoExtractResult& result) {
    // Locate End of Central Directory (EOCD) within last 64KB
    const off64_t searchStart = std::max<off64_t>(0, fileSize - 65557);
    const size_t searchLen = static_cast<size_t>(fileSize - searchStart);
    std::vector<uint8_t> tail(searchLen);
    if (!preadExact(fd, searchStart, tail.data(), searchLen)) {
        result.error = "Failed to read ZIP trailer";
        return false;
    }

    ssize_t eocdIdx = -1;
    for (ssize_t i = static_cast<ssize_t>(searchLen) - 22; i >= 0; --i) {
        if (tail[i] == 0x50 && tail[i + 1] == 0x4B && tail[i + 2] == 0x05 && tail[i + 3] == 0x06) {
            eocdIdx = i;
            break;
        }
    }
    if (eocdIdx < 0) {
        result.error = "Corrupt ZIP archive (EOCD signature missing)";
        return false;
    }

    auto u16 = [](const uint8_t* p) -> uint16_t {
        return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
    };

    const uint16_t totalEntries = u16(&tail[eocdIdx + 10]);
    const uint32_t cdSize = readLe32(&tail[eocdIdx + 12]);
    const uint32_t cdOffset = readLe32(&tail[eocdIdx + 16]);

    if (static_cast<off64_t>(cdOffset) + cdSize > fileSize) {
        result.error = "Invalid ZIP Central Directory bounds";
        return false;
    }

    std::vector<uint8_t> cd(cdSize);
    if (!preadExact(fd, cdOffset, cd.data(), cdSize)) {
        result.error = "Failed reading ZIP Central Directory";
        return false;
    }

    size_t pos = 0;
    for (uint16_t idx = 0; idx < totalEntries && pos + 46 <= cd.size(); ++idx) {
        if (cd[pos] != 0x50 || cd[pos + 1] != 0x4B || cd[pos + 2] != 0x01 || cd[pos + 3] != 0x02) {
            break;
        }
        uint16_t method = u16(&cd[pos + 10]);
        uint32_t compSize = readLe32(&cd[pos + 20]);
        uint32_t uncompSize = readLe32(&cd[pos + 24]);
        uint16_t nameLen = u16(&cd[pos + 28]);
        uint16_t extraLen = u16(&cd[pos + 30]);
        uint16_t commentLen = u16(&cd[pos + 32]);
        uint32_t localHeaderOffset = readLe32(&cd[pos + 42]);

        if (pos + 46 + nameLen > cd.size()) break;
        std::string entryName(reinterpret_cast<const char*>(&cd[pos + 46]), nameLen);
        pos += 46 + nameLen + extraLen + commentLen;

        // Path traversal guard + skip directories
        if (entryName.empty() || entryName.back() == '/' || entryName.find("..") != std::string::npos) {
            continue;
        }

        // Flatten filename for content directory or preserve relative if safe
        std::string baseName = fs::path(entryName).filename().string();
        std::string lowerBase = baseName;
        std::transform(lowerBase.begin(), lowerBase.end(), lowerBase.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (!isRelevantOpenRaAsset(lowerBase)) {
            continue;
        }

        uint8_t lfh[30]{};
        if (!preadExact(fd, localHeaderOffset, lfh, 30) ||
            lfh[0] != 0x50 || lfh[1] != 0x4B || lfh[2] != 0x03 || lfh[3] != 0x04) {
            result.error = "Corrupt ZIP local file header";
            return false;
        }
        uint16_t lfhNameLen = u16(&lfh[26]);
        uint16_t lfhExtraLen = u16(&lfh[28]);
        off64_t dataOffset = static_cast<off64_t>(localHeaderOffset) + 30 + lfhNameLen + lfhExtraLen;

        std::string outPath = outDir + "/" + lowerBase;
        std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            result.error = "Failed creating output file in app storage";
            return false;
        }

        if (method == 0) {
            // Stored (uncompressed)
            std::vector<uint8_t> buf(65536);
            uint32_t rem = compSize;
            off64_t curOff = dataOffset;
            while (rem > 0) {
                uint32_t chunk = std::min<uint32_t>(rem, static_cast<uint32_t>(buf.size()));
                if (!preadExact(fd, curOff, buf.data(), chunk)) {
                    result.error = "ZIP read error";
                    return false;
                }
                ofs.write(reinterpret_cast<const char*>(buf.data()), chunk);
                curOff += chunk;
                rem -= chunk;
            }
        } else if (method == 8) {
            // Deflate
            z_stream zs{};
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
                result.error = "zlib inflateInit2 failed";
                return false;
            }
            std::vector<uint8_t> inBuf(65536);
            std::vector<uint8_t> outBuf(65536);
            uint32_t rem = compSize;
            off64_t curOff = dataOffset;
            int zret = Z_OK;

            while (rem > 0 && zret != Z_STREAM_END) {
                uint32_t chunk = std::min<uint32_t>(rem, static_cast<uint32_t>(inBuf.size()));
                if (!preadExact(fd, curOff, inBuf.data(), chunk)) {
                    inflateEnd(&zs);
                    result.error = "ZIP deflate read error";
                    return false;
                }
                curOff += chunk;
                rem -= chunk;

                zs.avail_in = chunk;
                zs.next_in = inBuf.data();
                do {
                    zs.avail_out = static_cast<uInt>(outBuf.size());
                    zs.next_out = outBuf.data();
                    zret = inflate(&zs, Z_NO_FLUSH);
                    if (zret == Z_STREAM_ERROR || zret == Z_DATA_ERROR || zret == Z_MEM_ERROR) {
                        inflateEnd(&zs);
                        result.error = "Corrupt deflate stream in ZIP";
                        return false;
                    }
                    size_t have = outBuf.size() - zs.avail_out;
                    if (have > 0) {
                        ofs.write(reinterpret_cast<const char*>(outBuf.data()),
                                  static_cast<std::streamsize>(have));
                    }
                } while (zs.avail_out == 0);
            }
            inflateEnd(&zs);
        } else {
            continue;
        }

        result.filesExtracted++;
        result.bytesExtracted += uncompSize;
        int pct = 20 + static_cast<int>((70ULL * (idx + 1)) / std::max<uint16_t>(1, totalEntries));
        onProgress(pct, "Extracted " + lowerBase);
    }

    result.ok = (result.filesExtracted > 0);
    if (!result.ok && result.error.empty()) {
        result.error = "No OpenRA game assets (.mix/.aud/.vqa/.pak) found inside ZIP";
    }
    return result.ok;
}

std::string computeFdSha256(int fd, off64_t fileSize, const ProgressFn& onProgress) {
    Sha256 sha;
    std::vector<uint8_t> buf(262144);
    off64_t offset = 0;
    int lastPct = -1;
    while (offset < fileSize) {
        size_t chunk = static_cast<size_t>(std::min<off64_t>(buf.size(), fileSize - offset));
        if (!preadExact(fd, offset, buf.data(), chunk)) {
            break;
        }
        sha.update(buf.data(), chunk);
        offset += chunk;
        int pct = static_cast<int>((offset * 20ULL) / std::max<off64_t>(1, fileSize));
        if (pct != lastPct) {
            lastPct = pct;
            onProgress(pct, "Verifying archive integrity (SHA-256)...");
        }
    }
    return sha.finalHex();
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_org_openra_android_NativeBridge_nativeExtractAndVerify(
    JNIEnv* env,
    jobject /* thiz */,
    jint fd,
    jstring jDestDir,
    jobject jCallback) {

    const char* destChars = env->GetStringUTFChars(jDestDir, nullptr);
    std::string destDir = destChars ? destChars : "";
    if (destChars) env->ReleaseStringUTFChars(jDestDir, destChars);

    if (fd < 0 || destDir.empty()) {
        return env->NewStringUTF("ERROR|Invalid file descriptor or destination directory");
    }

    jclass cbClass = jCallback ? env->GetObjectClass(jCallback) : nullptr;
    jmethodID cbMethod = cbClass
        ? env->GetMethodID(cbClass, "onProgress", "(ILjava/lang/String;)V")
        : nullptr;

    auto report = [&](int pct, const std::string& msg) {
        if (jCallback && cbMethod) {
            jstring jMsg = env->NewStringUTF(msg.c_str());
            env->CallVoidMethod(jCallback, cbMethod, pct, jMsg);
            env->DeleteLocalRef(jMsg);
        }
    };

    struct stat64 st{};
    if (::fstat64(fd, &st) != 0 || st.st_size <= 0) {
        ::close(fd);
        return env->NewStringUTF("ERROR|Unable to stat selected file");
    }

    if (!ensureDir(destDir)) {
        ::close(fd);
        return env->NewStringUTF("ERROR|Failed to create content directory");
    }

    ArchiveFormat fmt = detectFormat(fd);
    if (fmt == ArchiveFormat::Unknown) {
        ::close(fd);
        return env->NewStringUTF("ERROR|Unsupported format. Select a valid .iso, .chd, or .zip game archive.");
    }

    std::string shaHex = computeFdSha256(fd, st.st_size, report);
    IsoExtractResult res;

    if (fmt == ArchiveFormat::Iso2048 || fmt == ArchiveFormat::Iso2352) {
        const bool isRaw2352 = (fmt == ArchiveFormat::Iso2352);
        SectorReadFn reader = [fd, isRaw2352](uint32_t lba, uint8_t* out2048) -> bool {
            off64_t off = isRaw2352
                ? (static_cast<off64_t>(lba) * 2352LL + 16LL)
                : (static_cast<off64_t>(lba) * 2048LL);
            return preadExact(fd, off, out2048, 2048);
        };
        extractIsoStream(reader, destDir, report, res);
    } else if (fmt == ArchiveFormat::Zip) {
        extractZipFd(fd, st.st_size, destDir, report, res);
    } else if (fmt == ArchiveFormat::Chd) {
#ifdef HAS_LIBCHDR
        ::lseek64(fd, 0, SEEK_SET);
        FILE* fp = ::fdopen(fd, "rb");
        if (!fp) {
            ::close(fd);
            return env->NewStringUTF("ERROR|Failed to open CHD stream");
        }
        chd_file* chd = nullptr;
        chd_error err = chd_open_file(fp, CHD_OPEN_READ, nullptr, &chd);
        if (err != CHDERR_NONE || !chd) {
            ::fclose(fp);
            return env->NewStringUTF("ERROR|Corrupt or unsupported CHD file");
        }
        const chd_header* hdr = chd_get_header(chd);
        const uint32_t hunkBytes = hdr->hunkbytes;
        const uint32_t unitBytes = hdr->unitbytes ? hdr->unitbytes : 2448;
        std::vector<uint8_t> hunkBuf(hunkBytes);
        uint32_t cachedHunk = UINT32_MAX;

        SectorReadFn reader = [&](uint32_t lba, uint8_t* out2048) -> bool {
            uint64_t byteOffset = static_cast<uint64_t>(lba) * unitBytes;
            uint32_t hunkIdx = static_cast<uint32_t>(byteOffset / hunkBytes);
            uint32_t inHunk = static_cast<uint32_t>(byteOffset % hunkBytes);
            if (hunkIdx != cachedHunk) {
                if (chd_read(chd, hunkIdx, hunkBuf.data()) != CHDERR_NONE) {
                    return false;
                }
                cachedHunk = hunkIdx;
            }
            if (inHunk + 2048 > hunkBytes) return false;
            const uint32_t dataShift = (unitBytes >= 2352) ? 16U : 0U;
            std::memcpy(out2048, hunkBuf.data() + inHunk + dataShift, 2048);
            return true;
        };

        extractIsoStream(reader, destDir, report, res);
        chd_close(chd);
        ::fclose(fp);
        fd = -1;
#else
        ::close(fd);
        return env->NewStringUTF("ERROR|libchdr not enabled in this build");
#endif
    }

    if (fd >= 0) {
        ::close(fd);
    }

    if (!res.ok) {
        std::string errOut = "ERROR|" + res.error;
        return env->NewStringUTF(errOut.c_str());
    }

    report(100, "Verified & ready (" + std::to_string(res.filesExtracted) + " files)");
    std::string okOut = "OK|" + std::to_string(res.filesExtracted) + "|" +
                        std::to_string(res.bytesExtracted) + "|" + shaHex;
    return env->NewStringUTF(okOut.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_openra_android_NativeBridge_nativeGetAbiInfo(JNIEnv* env, jobject /* thiz */) {
    return env->NewStringUTF("arm64-v8a (ARMv8-A / OpenGL ES 3.0 + Oboe)");
}

namespace {
using OpenRAEntryFn = int (*)(const char* engineDir, const char* supportDir, const char* modId);
OpenRAEntryFn g_openraEntry = nullptr;
std::string g_engineDir;
std::string g_supportDir;
std::string g_modId = "ra";
} // namespace

extern "C" JNIEXPORT void JNICALL
Java_org_openra_android_NativeBridge_nativeConfigureLaunch(
    JNIEnv* env,
    jobject /* thiz */,
    jlong entryFnPtr,
    jstring jEngineDir,
    jstring jSupportDir,
    jstring jModId) {
    g_openraEntry = reinterpret_cast<OpenRAEntryFn>(static_cast<uintptr_t>(entryFnPtr));
    const char* e = env->GetStringUTFChars(jEngineDir, nullptr);
    const char* s = env->GetStringUTFChars(jSupportDir, nullptr);
    const char* m = env->GetStringUTFChars(jModId, nullptr);
    if (e) { g_engineDir = e; env->ReleaseStringUTFChars(jEngineDir, e); }
    if (s) { g_supportDir = s; env->ReleaseStringUTFChars(jSupportDir, s); }
    if (m) { g_modId = m; env->ReleaseStringUTFChars(jModId, m); }
}

extern "C" int SDL_main(int /* argc */, char* /* argv */[]) {
    if (!g_openraEntry) {
        LOGE("SDL_main called before OpenRA entry point was registered!");
        return 1;
    }
    LOGI("Starting OpenRA C# engine: mod=%s engineDir=%s supportDir=%s",
         g_modId.c_str(), g_engineDir.c_str(), g_supportDir.c_str());
    return g_openraEntry(g_engineDir.c_str(), g_supportDir.c_str(), g_modId.c_str());
}
