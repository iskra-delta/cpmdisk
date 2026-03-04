#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

// ── Disk geometry ──────────────────────────────────────────────────────────────
// All field names follow the cpmtools diskdef convention.

struct disk_def {
    const char* name;
    uint32_t    seclen;     // physical sector size in bytes
    uint32_t    tracks;     // total number of tracks
    uint32_t    sectrk;     // sectors per track
    uint32_t    blocksize;  // allocation block size in bytes
    uint32_t    maxdir;     // maximum number of directory entries
    uint32_t    skew;       // sector skew factor (0 = no skew)
    uint32_t    boottrk;    // number of reserved boot tracks

    // ── Derived geometry ───────────────────────────────────────────────────────
    constexpr uint32_t total_sectors()   const noexcept { return tracks * sectrk; }
    constexpr uint32_t boot_sectors()    const noexcept { return boottrk * sectrk; }
    constexpr uint32_t data_sectors()    const noexcept { return total_sectors() - boot_sectors(); }
    constexpr uint32_t secs_per_block()  const noexcept { return blocksize / seclen; }
    constexpr uint32_t total_blocks()    const noexcept { return data_sectors() / secs_per_block(); }
    constexpr uint64_t disk_size()       const noexcept { return uint64_t(total_sectors()) * seclen; }

    // Number of data blocks used for the directory.
    constexpr uint32_t dir_blocks() const noexcept {
        return (maxdir * 32u + blocksize - 1u) / blocksize;
    }

    // Block pointers are 2 bytes (little-endian) when total_blocks > 255.
    constexpr bool     use_word_blocks()   const noexcept { return total_blocks() > 255; }
    constexpr uint32_t ptrs_per_extent()   const noexcept { return use_word_blocks() ? 8u : 16u; }

    // How many blocks one logical 16 KB extent actually uses.
    // CP/M always defines one logical extent = 128 records x 128 bytes = 16 384 bytes.
    constexpr uint32_t blocks_per_extent() const noexcept {
        uint32_t bpe = 16384u / blocksize;
        return bpe < ptrs_per_extent() ? bpe : ptrs_per_extent();
    }

    // Byte offset in the disk image for the first byte of a data block.
    constexpr uint64_t block_offset(uint32_t block) const noexcept {
        return uint64_t(boot_sectors() + block * secs_per_block()) * seclen;
    }
};

// ── Known disk definitions ─────────────────────────────────────────────────────

inline constexpr disk_def DISK_FDD {
    "idpfdd",
    /*seclen*/    256,
    /*tracks*/    146,
    /*sectrk*/    18,
    /*blocksize*/ 2048,
    /*maxdir*/    128,
    /*skew*/      0,
    /*boottrk*/   2
};

inline constexpr disk_def DISK_HDD {
    "idphdd",
    /*seclen*/    256,
    /*tracks*/    1224,
    /*sectrk*/    32,
    /*blocksize*/ 4096,
    /*maxdir*/    1024,
    /*skew*/      0,
    /*boottrk*/   1
};

// Look up a definition by short name ("fdd"/"hdd") or full name ("idpfdd"/"idphdd").
inline std::optional<disk_def> find_diskdef(std::string_view name) noexcept {
    if (name == "fdd" || name == "idpfdd") return DISK_FDD;
    if (name == "hdd" || name == "idphdd") return DISK_HDD;
    return std::nullopt;
}

inline std::optional<disk_def> find_diskdef_in_file(const std::filesystem::path& path,
                                                     std::string_view name) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open diskdefs file '" + path.string() + "'");

    auto trim = [](std::string s) {
        size_t b = 0;
        while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
        size_t e = s.size();
        while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
        return s.substr(b, e - b);
    };

    auto parse_u32 = [](const std::string& text, const char* key, uint32_t line_no) -> uint32_t {
        size_t pos = 0;
        unsigned long long v = 0;
        try {
            v = std::stoull(text, &pos, 0);
        } catch (...) {
            throw std::runtime_error("diskdefs parse error at line " + std::to_string(line_no) +
                                     ": invalid value for " + key + " ('" + text + "')");
        }
        if (pos != text.size() || v > std::numeric_limits<uint32_t>::max())
            throw std::runtime_error("diskdefs parse error at line " + std::to_string(line_no) +
                                     ": invalid value for " + key + " ('" + text + "')");
        return static_cast<uint32_t>(v);
    };

    bool in_def = false;
    bool target_def = false;
    std::string def_name;

    std::optional<uint32_t> seclen, tracks, sectrk, blocksize, maxdir, skew, boottrk, offset;

    auto clear_fields = [&]() {
        seclen.reset();
        tracks.reset();
        sectrk.reset();
        blocksize.reset();
        maxdir.reset();
        skew.reset();
        boottrk.reset();
        offset.reset();
    };

    std::string line;
    uint32_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        if (auto p = line.find('#'); p != std::string::npos) line.erase(p);
        if (auto p = line.find(';'); p != std::string::npos) line.erase(p);
        line = trim(std::move(line));
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string key;
        iss >> key;
        if (key.empty()) continue;

        if (key == "diskdef") {
            std::string nm;
            iss >> nm;
            if (nm.empty())
                throw std::runtime_error("diskdefs parse error at line " + std::to_string(line_no) +
                                         ": missing name after 'diskdef'");
            in_def = true;
            def_name = nm;
            target_def = (def_name == name);
            clear_fields();
            continue;
        }

        if (!in_def)
            continue;

        if (key == "end") {
            if (target_def) {
                if (!seclen || !tracks || !sectrk || !blocksize || !maxdir)
                    throw std::runtime_error("diskdefs '" + def_name +
                                             "' is missing one of required fields: "
                                             "seclen, tracks, sectrk, blocksize, maxdir");

                uint32_t eff_boottrk = 0;
                if (boottrk) eff_boottrk = *boottrk;
                else if (offset) {
                    if (*sectrk == 0 || (*offset % *sectrk) != 0)
                        throw std::runtime_error("diskdefs '" + def_name +
                                                 "': offset is not a whole number of tracks");
                    eff_boottrk = *offset / *sectrk;
                }

                return disk_def{
                    .name = "custom",
                    .seclen = *seclen,
                    .tracks = *tracks,
                    .sectrk = *sectrk,
                    .blocksize = *blocksize,
                    .maxdir = *maxdir,
                    .skew = skew.value_or(0),
                    .boottrk = eff_boottrk
                };
            }
            in_def = false;
            target_def = false;
            clear_fields();
            continue;
        }

        if (!target_def)
            continue;

        std::string value;
        iss >> value;
        if (value.empty())
            throw std::runtime_error("diskdefs parse error at line " + std::to_string(line_no) +
                                     ": missing value for '" + key + "'");

        if (key == "seclen")         seclen = parse_u32(value, "seclen", line_no);
        else if (key == "tracks")    tracks = parse_u32(value, "tracks", line_no);
        else if (key == "sectrk")    sectrk = parse_u32(value, "sectrk", line_no);
        else if (key == "blocksize") blocksize = parse_u32(value, "blocksize", line_no);
        else if (key == "maxdir")    maxdir = parse_u32(value, "maxdir", line_no);
        else if (key == "skew")      skew = parse_u32(value, "skew", line_no);
        else if (key == "boottrk")   boottrk = parse_u32(value, "boottrk", line_no);
        else if (key == "offset")    offset = parse_u32(value, "offset", line_no);
    }

    return std::nullopt;
}

inline std::optional<disk_def> find_diskdef(std::string_view name,
                                            const std::optional<std::filesystem::path>& diskdefs_file) {
    if (diskdefs_file) {
        if (auto d = find_diskdef_in_file(*diskdefs_file, name))
            return d;
    }
    return find_diskdef(name);
}

// Detect disk type from image file size.
inline std::optional<disk_def> diskdef_by_size(uint64_t size) noexcept {
    if (size == DISK_FDD.disk_size()) return DISK_FDD;
    if (size == DISK_HDD.disk_size()) return DISK_HDD;
    return std::nullopt;
}

// ── Geometry overrides ────────────────────────────────────────────────────────
//
// Holds optional per-field overrides that can be applied on top of any disk_def.
// Unset fields (std::nullopt) leave the base definition unchanged.

struct geo_opts {
    std::optional<uint32_t> seclen;
    std::optional<uint32_t> tracks;
    std::optional<uint32_t> sectrk;
    std::optional<uint32_t> blocksize;
    std::optional<uint32_t> maxdir;
    std::optional<uint32_t> skew;
    std::optional<uint32_t> boottrk;

    bool any() const noexcept {
        return seclen || tracks || sectrk || blocksize || maxdir || skew || boottrk;
    }

    // True when every field needed to describe a disk from scratch is set.
    // (skew is optional – it defaults to 0.)
    bool all_required() const noexcept {
        return seclen && tracks && sectrk && blocksize && maxdir && boottrk;
    }

    // Apply the set fields to `def`, marking the name as "custom" if anything changed.
    void apply_to(disk_def& def) const noexcept {
        if (seclen)    def.seclen    = *seclen;
        if (tracks)    def.tracks    = *tracks;
        if (sectrk)    def.sectrk    = *sectrk;
        if (blocksize) def.blocksize = *blocksize;
        if (maxdir)    def.maxdir    = *maxdir;
        if (skew)      def.skew      = *skew;
        if (boottrk)   def.boottrk   = *boottrk;
        if (any())     def.name      = "custom";
    }
};
