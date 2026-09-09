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

    // Optional DPB extent-mask override.  Unset means "derive from geometry",
    // which is what every standard CP/M format needs; see extent_mask() below.
    std::optional<uint8_t> exm{};

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

    // Block pointers are 2 bytes (little-endian) when the DPB's DSM (the highest
    // block number, i.e. total_blocks - 1) exceeds 255 — that is, when the disk
    // holds more than 256 allocation blocks.
    constexpr bool     use_word_blocks()   const noexcept { return total_blocks() > 256; }
    constexpr uint32_t ptrs_per_extent()   const noexcept { return use_word_blocks() ? 8u : 16u; }

    // How many blocks one logical 16 KB extent actually uses.
    // CP/M always defines one logical extent = 128 records x 128 bytes = 16 384 bytes.
    constexpr uint32_t blocks_per_extent() const noexcept {
        uint32_t bpe = 16384u / blocksize;
        return bpe < ptrs_per_extent() ? bpe : ptrs_per_extent();
    }

    // ── Extent mask (DPB "EXM") ────────────────────────────────────────────────
    //
    // A physical directory entry holds ptrs_per_extent() block pointers, which
    // address ptrs_per_extent() * blocksize bytes.  When that is more than one
    // 16 KB logical extent, the entry carries several logical extents at once:
    // XL/XH name the *last* logical extent stored in the entry and RC counts the
    // records of that last extent, while the earlier ones are implicitly full.
    //
    // The BDOS masks the FCB extent number with EXM before comparing it against
    // the directory, so writing one entry per logical extent on such a format
    // produces entries that all fold onto the same physical extent — only the
    // first is ever found and everything past it becomes unreachable.
    //
    // Derivation follows DRI's DPB table (and cpmtools):
    //   extents per entry = min(blocksize, 16384) * ptrs_per_extent() / 16384
    //   EXM               = extents per entry - 1
    //
    // e.g. blocksize 2048 with word pointers -> 1 extent/entry, EXM 0
    //      blocksize 4096 with word pointers -> 2 extents/entry, EXM 1
    //      blocksize 4096 with byte pointers -> 4 extents/entry, EXM 3

    constexpr uint32_t derived_extents_per_entry() const noexcept {
        uint32_t bs = blocksize < 16384u ? blocksize : 16384u;
        uint32_t n  = bs * ptrs_per_extent() / 16384u;
        return n == 0u ? 1u : n;  // degenerate; see extent_geometry_ok()
    }

    // Logical 16 KB extents carried by one physical directory entry (EXM + 1).
    constexpr uint32_t extents_per_entry() const noexcept {
        return exm ? uint32_t(*exm) + 1u : derived_extents_per_entry();
    }

    // The DPB extent mask itself.
    constexpr uint8_t extent_mask() const noexcept {
        return uint8_t(extents_per_entry() - 1u);
    }

    // Blocks addressable by one physical directory entry.
    constexpr uint32_t blocks_per_entry() const noexcept {
        uint64_t bytes  = uint64_t(extents_per_entry()) * 16384u;
        uint32_t blocks = uint32_t(bytes / blocksize);
        if (blocks == 0u) blocks = 1u;
        return blocks < ptrs_per_extent() ? blocks : ptrs_per_extent();
    }

    // False when a directory entry cannot even hold one full logical extent
    // (ptrs_per_extent() * blocksize < 16384).  CP/M has no encoding for that,
    // so such a geometry is not a valid CP/M format; the closest we can do is
    // one half-empty entry per logical extent.
    constexpr bool extent_geometry_ok() const noexcept {
        return uint64_t(ptrs_per_extent()) * blocksize >= 16384u;
    }

    // Byte offset in the disk image for the first byte of a data block.
    constexpr uint64_t block_offset(uint32_t block) const noexcept {
        return uint64_t(boot_sectors() + block * secs_per_block()) * seclen;
    }
};

// ── Known disk definitions ─────────────────────────────────────────────────────

// The Partner shipped two floppy media that share every CP/M parameter except
// the track count, so they differ only in capacity (DSM).  CP/M describes each
// physical head as a logical track, hence 2 x cylinders:
//
//   Partner G (GDP)  73 cylinders x 2 heads = 146 tracks -> 672 768 B
//   Partner P (CRT)  77 cylinders x 2 heads = 154 tracks -> 709 632 B
//
// `fdd` alone stays the Partner G medium.  Append ":g" or ":p" to name a model
// explicitly - see find_diskdef() below.

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

inline constexpr disk_def DISK_FDD_P {
    "idpfdd:p",
    /*seclen*/    256,
    /*tracks*/    154,
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

// ── Built-in format table ──────────────────────────────────────────────────────
//
// `model` is the Partner model letter the medium belongs to.  The first row for
// a base name is what that name alone resolves to, so adding media never changes
// an existing default.

struct named_disk {
    std::string_view shortname;
    std::string_view fullname;
    char             model;
    const disk_def*  def;
};

inline constexpr named_disk KNOWN_DISKS[] = {
    { "fdd", "idpfdd", 'g', &DISK_FDD   },  // Partner G, 146 tracks (default)
    { "fdd", "idpfdd", 'p', &DISK_FDD_P },  // Partner P, 154 tracks
    { "hdd", "idphdd", 'g', &DISK_HDD   },  // one 10 MB drive serves both models
    { "hdd", "idphdd", 'p', &DISK_HDD   },
};

// Look up a definition by short name ("fdd"/"hdd") or full name ("idpfdd"/
// "idphdd"), optionally suffixed with ":g" or ":p" to name a Partner model -
// "fdd:g" for Partner G, "fdd:p" for Partner P.  The suffix is case-insensitive,
// and a bare name resolves to that family's default medium.
inline std::optional<disk_def> find_diskdef(std::string_view name) noexcept {
    std::string_view base  = name;
    char             model = '\0';

    if (auto colon = name.rfind(':'); colon != std::string_view::npos) {
        base = name.substr(0, colon);
        std::string_view suffix = name.substr(colon + 1);
        if (suffix.size() != 1) return std::nullopt;
        model = suffix.front();
        if (model >= 'A' && model <= 'Z') model += 'a' - 'A';
    }

    for (const auto& d : KNOWN_DISKS) {
        if (base != d.shortname && base != d.fullname) continue;
        if (model != '\0' && model != d.model) continue;
        return *d.def;
    }
    return std::nullopt;
}

// True when `name` (with or without a ":<KiB>" suffix) names a built-in format.
inline bool is_builtin_diskdef(std::string_view name) noexcept {
    return find_diskdef(name).has_value();
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
    std::optional<uint32_t> exm_key;   // cpmdisk extension, see disk_def::exm

    auto clear_fields = [&]() {
        seclen.reset();
        tracks.reset();
        sectrk.reset();
        blocksize.reset();
        maxdir.reset();
        skew.reset();
        boottrk.reset();
        offset.reset();
        exm_key.reset();
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

                if (exm_key && *exm_key != 0 && *exm_key != 1 &&
                    *exm_key != 3 && *exm_key != 7 && *exm_key != 15)
                    throw std::runtime_error("diskdefs '" + def_name +
                                             "': exm must be one of 0, 1, 3, 7, 15");

                return disk_def{
                    .name = "custom",
                    .seclen = *seclen,
                    .tracks = *tracks,
                    .sectrk = *sectrk,
                    .blocksize = *blocksize,
                    .maxdir = *maxdir,
                    .skew = skew.value_or(0),
                    .boottrk = eff_boottrk,
                    .exm = exm_key ? std::optional<uint8_t>(uint8_t(*exm_key)) : std::nullopt
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
        // cpmdisk extension: an explicit extent mask for BIOSes whose DPB does
        // not follow the standard derivation.  Unknown to cpmtools, ignored by
        // it, and optional here - omit it and the mask is derived.
        else if (key == "exm")       exm_key = parse_u32(value, "exm", line_no);
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
    for (const auto& d : KNOWN_DISKS)
        if (size == d.def->disk_size()) return *d.def;
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
    std::optional<uint32_t> exm;   // extent-mask override; see disk_def::extent_mask()

    bool any() const noexcept {
        return seclen || tracks || sectrk || blocksize || maxdir || skew || boottrk || exm;
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
        if (exm)       def.exm       = uint8_t(*exm);
        if (any())     def.name      = "custom";
    }
};
