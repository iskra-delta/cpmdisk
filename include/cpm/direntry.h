#pragma once

#include <cstdint>
#include <cstring>
#include <string>

// ── CP/M 2.2 directory entry – exactly 32 bytes ────────────────────────────────
//
//  Byte  Field  Meaning
//  ----  -----  -------
//   0     ST    Status: 0-15 = user area, 0xE5 = deleted/free
//   1-8   FN    Filename, space-padded; high bits carry R/O, SYS, Archive attrs
//   9-11  FT    Extension, space-padded; high bits carry attrs
//  12     XL    Extent counter low  (bits 4:0)
//  13     BC    Byte count last record (CP/M 3 only; always 0 here)
//  14     XH    Extent counter high (bits 5:0)
//  15     RC    128-byte record count for this extent (0-128)
//  16-31  AL    Allocation block list (1- or 2-byte block numbers)

#pragma pack(push, 1)
struct dir_entry {
    uint8_t user;     // ST
    uint8_t name[8];  // FN
    uint8_t ext[3];   // FT
    uint8_t xl;       // XL
    uint8_t bc;       // BC
    uint8_t xh;       // XH
    uint8_t rc;       // RC
    uint8_t al[16];   // AL
};
#pragma pack(pop)

static_assert(sizeof(dir_entry) == 32, "dir_entry must be exactly 32 bytes");

// ── Predicates ────────────────────────────────────────────────────────────────

inline bool entry_is_free(const dir_entry& e) noexcept  { return e.user == 0xE5; }
inline bool entry_is_valid(const dir_entry& e) noexcept { return e.user <= 15; }

// Logical extent number = XH[5:0] * 32 + XL[4:0]
inline uint32_t extent_num(const dir_entry& e) noexcept {
    return uint32_t(e.xh & 0x3F) * 32u + uint32_t(e.xl & 0x1F);
}

// ── Name helpers ──────────────────────────────────────────────────────────────

// Return bare "NAME" part (high attribute bits stripped, trailing spaces removed).
inline std::string entry_stem(const dir_entry& e) {
    std::string s;
    for (int i = 0; i < 8; ++i) {
        char c = char(e.name[i] & 0x7F);
        if (c == ' ') break;
        s += c;
    }
    return s;
}

// Return bare "EXT" part.
inline std::string entry_extension(const dir_entry& e) {
    std::string s;
    for (int i = 0; i < 3; ++i) {
        char c = char(e.ext[i] & 0x7F);
        if (c == ' ') break;
        s += c;
    }
    return s;
}

// Return "NAME.EXT" (no dot when extension is empty).
inline std::string entry_filename(const dir_entry& e) {
    std::string ext = entry_extension(e);
    return ext.empty() ? entry_stem(e) : entry_stem(e) + '.' + ext;
}
