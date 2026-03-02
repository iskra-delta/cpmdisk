#include "cpm_disk.h"
#include "print_compat.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <stdexcept>
#include <tuple>

// ── Internal helpers ──────────────────────────────────────────────────────────

namespace {

// Convert a host basename (e.g. "hello.com") to an 8+3 CP/M name.
// Returns name[8] and ext[3], space-padded, uppercased.
// Throws on names that don't fit.
auto to_cpm_83(const std::string& basename)
    -> std::pair<std::array<char,8>, std::array<char,3>>
{
    std::string upper = basename;
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    auto dot = upper.rfind('.');
    std::string n = (dot == std::string::npos) ? upper : upper.substr(0, dot);
    std::string e = (dot == std::string::npos) ? ""    : upper.substr(dot + 1);

    if (n.empty())
        throw std::runtime_error(std::format("'{}': empty filename", basename));
    if (n.size() > 8)
        throw std::runtime_error(std::format("'{}': name part exceeds 8 characters", basename));
    if (e.size() > 3)
        throw std::runtime_error(std::format("'{}': extension exceeds 3 characters", basename));

    std::array<char,8> name_arr; name_arr.fill(' ');
    std::array<char,3> ext_arr;  ext_arr.fill(' ');
    std::copy(n.begin(), n.end(), name_arr.begin());
    std::copy(e.begin(), e.end(), ext_arr.begin());
    return {name_arr, ext_arr};
}

// Case-insensitive wildcard match (* and ? supported) on whole string.
bool wildcard_match(std::string_view pattern, std::string_view text) noexcept {
    if (pattern.empty()) return text.empty();
    if (pattern.front() == '*') {
        for (size_t i = 0; i <= text.size(); ++i)
            if (wildcard_match(pattern.substr(1), text.substr(i)))
                return true;
        return false;
    }
    if (text.empty()) return false;
    if (pattern.front() == '?' ||
        std::toupper(static_cast<unsigned char>(pattern.front())) ==
        std::toupper(static_cast<unsigned char>(text.front())))
    {
        return wildcard_match(pattern.substr(1), text.substr(1));
    }
    return false;
}

std::string to_upper(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Human-readable byte count.
std::string human_size(uint64_t bytes) {
    if (bytes >= 1024*1024)
        return std::format("{:.1f} MB", double(bytes) / (1024.0*1024.0));
    if (bytes >= 1024)
        return std::format("{:.1f} KB", double(bytes) / 1024.0);
    return std::format("{} B", bytes);
}

} // namespace

// ── Constructor ───────────────────────────────────────────────────────────────

CpmDisk::CpmDisk(std::filesystem::path path, DiskDef def)
    : path_(std::move(path)), def_(def)
{
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_)
        throw std::runtime_error(std::format("cannot open '{}': {}",
            path_.string(), std::strerror(errno)));
}

// ── Factory: create ───────────────────────────────────────────────────────────

CpmDisk CpmDisk::create(const std::filesystem::path& path, const DiskDef& def) {
    // Write the blank image (all zeros).
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
            throw std::runtime_error(std::format("cannot create '{}': {}",
                path.string(), std::strerror(errno)));

        constexpr size_t CHUNK = 65536;
        std::vector<uint8_t> zeros(CHUNK, 0);
        uint64_t remaining = def.disk_size();
        while (remaining > 0) {
            auto n = std::min(remaining, uint64_t(CHUNK));
            f.write(reinterpret_cast<const char*>(zeros.data()), std::streamsize(n));
            remaining -= n;
        }
    }

    // Open and initialise the directory area with 0xE5 (free entry marker).
    CpmDisk disk(path, def);
    std::vector<DirEntry> dir(def.maxdir);
    std::memset(dir.data(), 0xE5, def.maxdir * sizeof(DirEntry));
    disk.write_dir(dir);
    return disk;
}

// ── Factory: open ─────────────────────────────────────────────────────────────

CpmDisk CpmDisk::open(const std::filesystem::path& path) {
    auto sz = std::filesystem::file_size(path);
    auto opt = diskdef_by_size(sz);
    if (!opt)
        throw std::runtime_error(std::format(
            "'{}': unknown disk size {} bytes (expected {} for fdd or {} for hdd)",
            path.string(), sz, DISK_FDD.disk_size(), DISK_HDD.disk_size()));
    return CpmDisk(path, *opt);
}

// ── Low-level I/O ─────────────────────────────────────────────────────────────

std::vector<DirEntry> CpmDisk::read_dir() const {
    uint64_t offset = uint64_t(def_.boot_sectors()) * def_.seclen;
    file_.seekg(std::streamoff(offset));
    if (!file_)
        throw std::runtime_error("seek failed while reading directory");

    std::vector<DirEntry> dir(def_.maxdir);
    file_.read(reinterpret_cast<char*>(dir.data()),
               std::streamsize(def_.maxdir * sizeof(DirEntry)));
    if (!file_)
        throw std::runtime_error("read failed while reading directory");
    return dir;
}

void CpmDisk::write_dir(const std::vector<DirEntry>& dir) {
    assert(dir.size() == def_.maxdir);
    uint64_t offset = uint64_t(def_.boot_sectors()) * def_.seclen;
    file_.seekp(std::streamoff(offset));
    file_.write(reinterpret_cast<const char*>(dir.data()),
                std::streamsize(def_.maxdir * sizeof(DirEntry)));
    file_.flush();
    if (!file_)
        throw std::runtime_error("write failed while writing directory");
}

std::vector<uint8_t> CpmDisk::read_block(uint32_t block) const {
    std::vector<uint8_t> buf(def_.blocksize);
    file_.seekg(std::streamoff(def_.block_offset(block)));
    file_.read(reinterpret_cast<char*>(buf.data()), std::streamsize(def_.blocksize));
    if (!file_)
        throw std::runtime_error(std::format("read failed for block {}", block));
    return buf;
}

void CpmDisk::write_block(uint32_t block, std::span<const uint8_t> data) {
    assert(data.size() == def_.blocksize);
    file_.seekp(std::streamoff(def_.block_offset(block)));
    file_.write(reinterpret_cast<const char*>(data.data()),
                std::streamsize(def_.blocksize));
    if (!file_)
        throw std::runtime_error(std::format("write failed for block {}", block));
}

// ── Allocation ────────────────────────────────────────────────────────────────

std::set<uint32_t> CpmDisk::used_blocks(const std::vector<DirEntry>& dir) const {
    std::set<uint32_t> used;

    // Directory blocks are permanently allocated (blocks 0 .. dir_blocks-1).
    for (uint32_t i = 0; i < def_.dir_blocks(); ++i)
        used.insert(i);

    for (const auto& e : dir) {
        if (!entry_is_valid(e)) continue;
        for (uint32_t i = 0; i < def_.ptrs_per_extent(); ++i) {
            uint32_t blk;
            if (def_.use_word_blocks())
                blk = uint32_t(e.al[i*2]) | (uint32_t(e.al[i*2+1]) << 8);
            else
                blk = e.al[i];
            if (blk != 0) used.insert(blk);
        }
    }
    return used;
}

uint32_t CpmDisk::alloc_block(std::set<uint32_t>& used) const {
    for (uint32_t i = def_.dir_blocks(); i < def_.total_blocks(); ++i) {
        if (!used.contains(i)) {
            used.insert(i);
            return i;
        }
    }
    throw std::runtime_error("disk is full – no free blocks available");
}

// ── cmd_info ──────────────────────────────────────────────────────────────────

void CpmDisk::cmd_info() {
    auto dir  = read_dir();
    auto used = used_blocks(dir);

    uint32_t free_blk  = def_.total_blocks() - uint32_t(used.size());
    uint32_t used_dir  = 0;
    for (const auto& e : dir) if (entry_is_valid(e)) ++used_dir;

    println("Disk image : {}", path_.string());
    println("Disk type  : {}", def_.name);
    println("");
    println("Geometry");
    println("  Tracks          : {}", def_.tracks);
    println("  Sectors / track : {}", def_.sectrk);
    println("  Sector size     : {} bytes", def_.seclen);
    println("  Boot tracks     : {}", def_.boottrk);
    println("  Block size      : {} bytes", def_.blocksize);
    println("  Total blocks    : {}", def_.total_blocks());
    println("  Directory blocks: {}", def_.dir_blocks());
    println("  Disk size       : {} ({})", def_.disk_size(), human_size(def_.disk_size()));
    println("");
    println("Directory");
    println("  Capacity        : {} entries", def_.maxdir);
    println("  Used            : {}", used_dir);
    println("  Free            : {}", def_.maxdir - used_dir);
    println("");
    println("Allocation");
    println("  Total blocks    : {}", def_.total_blocks());
    println("  Used blocks     : {}", used.size());
    println("  Free blocks     : {}", free_blk);
    println("  Free space      : {} ({})",
        uint64_t(free_blk) * def_.blocksize,
        human_size(uint64_t(free_blk) * def_.blocksize));
}

// ── cmd_list ──────────────────────────────────────────────────────────────────

void CpmDisk::cmd_list(int user) {
    auto dir = read_dir();

    // Group extents by (user, filename) to compute file sizes.
    // Key: (user, filename).  Value: (max_extent_num, rc_of_that_extent).
    using FileKey = std::tuple<uint8_t, std::string>;
    struct FileAcc { uint32_t max_ext{0}; uint8_t last_rc{0}; };
    std::map<FileKey, FileAcc> files;

    for (const auto& e : dir) {
        if (!entry_is_valid(e)) continue;
        if (user >= 0 && e.user != uint8_t(user)) continue;

        FileKey key{e.user, entry_filename(e)};
        uint32_t ext = extent_num(e);
        auto& acc = files[key];
        if (ext >= acc.max_ext) {
            acc.max_ext  = ext;
            acc.last_rc  = e.rc;
        }
    }

    if (files.empty()) {
        println("No files found.");
        return;
    }

    constexpr uint32_t EXTENT_BYTES = 128u * 128u; // 16 384 bytes per extent

    println("{:>4}  {:<12}  {:>10}  {:>10}", "User", "Name", "Size", "");
    println("{:->4}  {:-<12}  {:->10}  {:->10}", "", "", "", "");

    for (const auto& [key, acc] : files) {
        auto [u, fname] = key;
        uint64_t size = uint64_t(acc.max_ext) * EXTENT_BYTES
                      + uint64_t(acc.last_rc) * 128u;
        println("{:>4}  {:<12}  {:>10}  {:>10}",
            u, fname, size, human_size(size));
    }
    println("");
    println("{} file(s)", files.size());
}

// ── cmd_add ───────────────────────────────────────────────────────────────────

void CpmDisk::cmd_add(const std::filesystem::path& host_path, int user) {
    if (user < 0 || user > 15)
        throw std::runtime_error(std::format("invalid user area {} (must be 0-15)", user));

    // ── Read host file ────────────────────────────────────────────────────────
    if (!std::filesystem::exists(host_path))
        throw std::runtime_error(std::format("'{}': file not found", host_path.string()));

    auto file_size = std::filesystem::file_size(host_path);
    std::vector<uint8_t> data(file_size);
    {
        std::ifstream f(host_path, std::ios::binary);
        if (!f)
            throw std::runtime_error(std::format("cannot open '{}' for reading", host_path.string()));
        f.read(reinterpret_cast<char*>(data.data()), std::streamsize(file_size));
    }

    // ── Convert to CP/M 8.3 name ─────────────────────────────────────────────
    auto [cpm_name, cpm_ext] = to_cpm_83(host_path.filename().string());

    // ── Compute allocation requirements ──────────────────────────────────────
    uint32_t bpe            = def_.blocks_per_extent();
    uint32_t blocks_needed  = uint32_t((file_size + def_.blocksize - 1) / def_.blocksize);
    uint32_t extents_needed = (blocks_needed == 0) ? 1u
                            : (blocks_needed + bpe - 1) / bpe;

    // ── Load directory and check capacity ─────────────────────────────────────
    auto dir  = read_dir();
    auto used = used_blocks(dir);

    uint32_t free_blocks = def_.total_blocks() - uint32_t(used.size());
    if (blocks_needed > free_blocks)
        throw std::runtime_error(std::format(
            "'{}': need {} blocks, only {} free",
            host_path.filename().string(), blocks_needed, free_blocks));

    uint32_t free_entries = 0;
    for (const auto& e : dir) if (entry_is_free(e)) ++free_entries;
    if (extents_needed > free_entries)
        throw std::runtime_error(std::format(
            "'{}': need {} directory entries, only {} free",
            host_path.filename().string(), extents_needed, free_entries));

    // ── Allocate data blocks ──────────────────────────────────────────────────
    std::vector<uint32_t> alloc(blocks_needed);
    for (auto& blk : alloc) blk = alloc_block(used);

    // ── Write file data into blocks (last block padded with 0x1A) ─────────────
    for (uint32_t i = 0; i < blocks_needed; ++i) {
        std::vector<uint8_t> buf(def_.blocksize, 0x1Au);
        uint64_t src_offset = uint64_t(i) * def_.blocksize;
        uint64_t copy_len   = std::min(uint64_t(def_.blocksize), file_size - src_offset);
        std::copy(data.begin() + std::ptrdiff_t(src_offset),
                  data.begin() + std::ptrdiff_t(src_offset + copy_len),
                  buf.begin());
        write_block(alloc[i], buf);
    }

    // ── Write directory entries (one per extent) ──────────────────────────────
    uint32_t ext_idx      = 0;
    uint32_t blocks_done  = 0;
    uint64_t bytes_left   = file_size;

    for (auto& e : dir) {
        if (!entry_is_free(e)) continue;
        if (ext_idx >= extents_needed) break;

        std::memset(&e, 0, sizeof(DirEntry));
        e.user = uint8_t(user);
        std::memcpy(e.name, cpm_name.data(), 8);
        std::memcpy(e.ext,  cpm_ext.data(),  3);
        e.xl = uint8_t(ext_idx & 0x1Fu);
        e.xh = uint8_t((ext_idx >> 5) & 0x3Fu);

        uint32_t blks_this = std::min(bpe, blocks_needed - blocks_done);

        // Fill block pointer slots.
        if (def_.use_word_blocks()) {
            for (uint32_t i = 0; i < blks_this; ++i) {
                uint16_t bn = uint16_t(alloc[blocks_done + i]);
                e.al[i*2]   = uint8_t(bn & 0xFFu);
                e.al[i*2+1] = uint8_t(bn >> 8);
            }
        } else {
            for (uint32_t i = 0; i < blks_this; ++i)
                e.al[i] = uint8_t(alloc[blocks_done + i] & 0xFFu);
        }

        // Record count: how many 128-byte records in this extent.
        uint64_t extent_bytes = std::min(uint64_t(blks_this) * def_.blocksize, bytes_left);
        e.rc = uint8_t((extent_bytes + 127u) / 128u);

        blocks_done += blks_this;
        bytes_left  -= extent_bytes;
        ++ext_idx;
    }

    write_dir(dir);

    // Build display name from the 8+3 arrays we computed earlier.
    std::string display_name(cpm_name.data(), 8);
    while (!display_name.empty() && display_name.back() == ' ') display_name.pop_back();
    std::string display_ext(cpm_ext.data(), 3);
    while (!display_ext.empty() && display_ext.back() == ' ') display_ext.pop_back();
    if (!display_ext.empty()) display_name += '.' + display_ext;

    println("Added  {:>4}:{}  ({}, {} block(s))",
        user, display_name, human_size(file_size), blocks_needed);
}

// ── cmd_remove ────────────────────────────────────────────────────────────────

void CpmDisk::cmd_remove(const std::string& pattern, int user) {
    std::string up_pattern = to_upper(pattern);
    auto dir = read_dir();
    int removed = 0;

    for (auto& e : dir) {
        if (!entry_is_valid(e)) continue;
        if (user >= 0 && e.user != uint8_t(user)) continue;

        std::string fname = to_upper(entry_filename(e));
        if (wildcard_match(up_pattern, fname)) {
            println("Removing {:>4}:{}", int(e.user), entry_filename(e));
            e.user = 0xE5;  // mark all extents of this file as deleted
            ++removed;
        }
    }

    if (removed == 0)
        println("No files matched '{}'.", pattern);
    else {
        write_dir(dir);
        println("Removed {} directory entr{}.", removed, removed == 1 ? "y" : "ies");
    }
}
