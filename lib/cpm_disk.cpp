#include <cpm/cpm_disk.h>
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

uint32_t get_ptr_block(const dir_entry& e, const disk_def& def, uint32_t i) {
    if (def.use_word_blocks())
        return uint32_t(e.al[i * 2]) | (uint32_t(e.al[i * 2 + 1]) << 8);
    return e.al[i];
}

std::string cpm_name_from_arrays(const std::array<char,8>& n, const std::array<char,3>& e) {
    std::string name(n.data(), 8);
    while (!name.empty() && name.back() == ' ') name.pop_back();
    std::string ext(e.data(), 3);
    while (!ext.empty() && ext.back() == ' ') ext.pop_back();
    if (!ext.empty()) name += '.' + ext;
    return name;
}

struct file_record {
    uint8_t              user;
    std::string          filename;
    uint64_t             size;
    std::vector<uint32_t> blocks;
    std::vector<size_t>  entry_indices;
};

std::vector<file_record> collect_files(const std::vector<dir_entry>& dir,
                                       const disk_def& def,
                                       int user_filter) {
    struct extent_ref {
        uint32_t ext;
        size_t   idx;
    };
    struct grouped_file {
        std::map<uint32_t, extent_ref> extents;
        std::vector<size_t>            entry_indices;
        uint32_t                       max_ext{0};
        uint8_t                        last_rc{0};
    };

    using key_t = std::tuple<uint8_t, std::string>;
    std::map<key_t, grouped_file> grouped;

    for (size_t i = 0; i < dir.size(); ++i) {
        const auto& e = dir[i];
        if (!entry_is_valid(e)) continue;
        if (user_filter >= 0 && e.user != uint8_t(user_filter)) continue;

        key_t key{e.user, entry_filename(e)};
        auto& g = grouped[key];
        uint32_t ext = extent_num(e);

        g.entry_indices.push_back(i);
        g.extents.try_emplace(ext, extent_ref{ext, i});
        if (ext >= g.max_ext) {
            g.max_ext = ext;
            g.last_rc = e.rc;
        }
    }

    std::vector<file_record> out;
    for (auto& [key, g] : grouped) {
        auto [u, fname] = key;
        file_record rec;
        rec.user = u;
        rec.filename = fname;
        rec.size = uint64_t(g.max_ext) * 16384u + uint64_t(g.last_rc) * 128u;
        rec.entry_indices = g.entry_indices;

        for (const auto& [ext_no, ext_ref] : g.extents) {
            (void)ext_no;
            const auto& e = dir[ext_ref.idx];
            for (uint32_t i = 0; i < def.ptrs_per_extent(); ++i) {
                uint32_t blk = get_ptr_block(e, def, i);
                if (blk != 0) rec.blocks.push_back(blk);
            }
        }
        out.push_back(std::move(rec));
    }
    return out;
}

} // namespace

// ── Constructor ───────────────────────────────────────────────────────────────

cpm_disk::cpm_disk(std::filesystem::path path, disk_def def)
    : path_(std::move(path)), def_(def)
{
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_)
        throw std::runtime_error(std::format("cannot open '{}': {}",
            path_.string(), std::strerror(errno)));
}

// ── Factory: create ───────────────────────────────────────────────────────────

cpm_disk cpm_disk::create(const std::filesystem::path& path,
                          const disk_def& def,
                          const create_opts& opts) {
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

    // Fill the entire physical directory allocation with 0xE5 (free entry marker).
    // We fill dir_blocks() * blocksize bytes, not just maxdir * 32 bytes, so that
    // any unoccupied space in the last directory block is also marked free.  This
    // prevents spurious "used" entries if the disk is later opened with a larger
    // maxdir (e.g. when overriding a standard type).
    cpm_disk disk(path, def);
    uint64_t dir_area = uint64_t(def.dir_blocks()) * def.blocksize;
    std::vector<uint8_t> e5(dir_area, 0xE5u);
    disk.file_.seekp(std::streamoff(def.block_offset(0)));
    disk.file_.write(reinterpret_cast<const char*>(e5.data()), std::streamsize(dir_area));

    if (!opts.label.empty() || opts.datestamp) {
        auto dir = disk.read_dir();
        size_t slot = 0;

        if (!opts.label.empty()) {
            if (slot >= dir.size())
                throw std::runtime_error("CP/M 3 label requested but no directory slots available");

            auto [lbl_name, lbl_ext] = to_cpm_83(opts.label);
            std::memset(&dir[slot], 0, sizeof(dir_entry));
            dir[slot].user = 0x20; // CP/M 3 label marker entry
            std::memcpy(dir[slot].name, lbl_name.data(), 8);
            std::memcpy(dir[slot].ext,  lbl_ext.data(),  3);
            ++slot;
        }

        if (opts.datestamp) {
            if (slot >= dir.size())
                throw std::runtime_error("CP/M 3 datestamp requested but no directory slots available");

            std::memset(&dir[slot], 0, sizeof(dir_entry));
            dir[slot].user = 0x21; // CP/M 3 datestamp marker entry
            ++slot;
        }

        disk.write_dir(dir);
    }

    disk.file_.flush();
    return disk;
}

// ── Factory: open ─────────────────────────────────────────────────────────────

cpm_disk cpm_disk::open(const std::filesystem::path& path, std::optional<disk_def> hint) {
    if (hint)
        return cpm_disk(path, *hint);

    auto sz  = std::filesystem::file_size(path);
    auto opt = diskdef_by_size(sz);
    if (!opt)
        throw std::runtime_error(std::format(
            "'{}': unrecognised disk size {} bytes – use -f/--format or geometry flags",
            path.string(), sz));
    return cpm_disk(path, *opt);
}

// ── Low-level I/O ─────────────────────────────────────────────────────────────

std::vector<dir_entry> cpm_disk::read_dir() const {
    uint64_t offset = uint64_t(def_.boot_sectors()) * def_.seclen;
    file_.seekg(std::streamoff(offset));
    if (!file_)
        throw std::runtime_error("seek failed while reading directory");

    std::vector<dir_entry> dir(def_.maxdir);
    file_.read(reinterpret_cast<char*>(dir.data()),
               std::streamsize(def_.maxdir * sizeof(dir_entry)));
    if (!file_)
        throw std::runtime_error("read failed while reading directory");
    return dir;
}

void cpm_disk::write_dir(const std::vector<dir_entry>& dir) {
    assert(dir.size() == def_.maxdir);
    uint64_t offset = uint64_t(def_.boot_sectors()) * def_.seclen;
    file_.seekp(std::streamoff(offset));
    file_.write(reinterpret_cast<const char*>(dir.data()),
                std::streamsize(def_.maxdir * sizeof(dir_entry)));
    file_.flush();
    if (!file_)
        throw std::runtime_error("write failed while writing directory");
}

std::vector<uint8_t> cpm_disk::read_block(uint32_t block) const {
    std::vector<uint8_t> buf(def_.blocksize);
    file_.seekg(std::streamoff(def_.block_offset(block)));
    file_.read(reinterpret_cast<char*>(buf.data()), std::streamsize(def_.blocksize));
    if (!file_)
        throw std::runtime_error(std::format("read failed for block {}", block));
    return buf;
}

void cpm_disk::write_block(uint32_t block, std::span<const uint8_t> data) {
    assert(data.size() == def_.blocksize);
    file_.seekp(std::streamoff(def_.block_offset(block)));
    file_.write(reinterpret_cast<const char*>(data.data()),
                std::streamsize(def_.blocksize));
    if (!file_)
        throw std::runtime_error(std::format("write failed for block {}", block));
}

// ── Allocation ────────────────────────────────────────────────────────────────

std::set<uint32_t> cpm_disk::used_blocks(const std::vector<dir_entry>& dir) const {
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

uint32_t cpm_disk::alloc_block(std::set<uint32_t>& used) const {
    for (uint32_t i = def_.dir_blocks(); i < def_.total_blocks(); ++i) {
        if (!used.contains(i)) {
            used.insert(i);
            return i;
        }
    }
    throw std::runtime_error("disk is full – no free blocks available");
}

// ── cmd_info ──────────────────────────────────────────────────────────────────

void cpm_disk::cmd_info() {
    auto dir  = read_dir();
    auto used = used_blocks(dir);

    uint32_t free_blk   = def_.total_blocks() - uint32_t(used.size());
    uint32_t file_dir   = 0;
    uint32_t free_dir   = 0;
    uint32_t cpm3_meta  = 0;
    for (const auto& e : dir) {
        if (entry_is_valid(e)) ++file_dir;
        else if (entry_is_free(e)) ++free_dir;
        else if (e.user == 0x20 || e.user == 0x21) ++cpm3_meta;
    }

    pc::println("Disk image : {}", path_.string());
    pc::println("Disk type  : {}", def_.name);
    pc::println("");
    pc::println("Geometry");
    pc::println("  Tracks          : {}", def_.tracks);
    pc::println("  Sectors / track : {}", def_.sectrk);
    pc::println("  Sector size     : {} bytes", def_.seclen);
    pc::println("  Boot tracks     : {}", def_.boottrk);
    pc::println("  Block size      : {} bytes", def_.blocksize);
    pc::println("  Total blocks    : {}", def_.total_blocks());
    pc::println("  Directory blocks: {}", def_.dir_blocks());
    pc::println("  Disk size       : {} ({})", def_.disk_size(), human_size(def_.disk_size()));
    pc::println("");
    pc::println("Directory");
    pc::println("  Capacity        : {} entries", def_.maxdir);
    pc::println("  Used (files)    : {}", file_dir);
    if (cpm3_meta > 0)
        pc::println("  Used (CP/M 3)   : {}", cpm3_meta);
    pc::println("  Free            : {}", free_dir);
    pc::println("");
    pc::println("Allocation");
    pc::println("  Total blocks    : {}", def_.total_blocks());
    pc::println("  Used blocks     : {}", used.size());
    pc::println("  Free blocks     : {}", free_blk);
    pc::println("  Free space      : {} ({})",
        uint64_t(free_blk) * def_.blocksize,
        human_size(uint64_t(free_blk) * def_.blocksize));
}

// ── cmd_list ──────────────────────────────────────────────────────────────────

void cpm_disk::cmd_list(int user) {
    auto dir = read_dir();

    // Group extents by (user, filename) to compute file sizes.
    // Key: (user, filename).  Value: (max_extent_num, rc_of_that_extent).
    using FileKey = std::tuple<uint8_t, std::string>;
    struct file_acc { uint32_t max_ext{0}; uint8_t last_rc{0}; };
    std::map<FileKey, file_acc> files;

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
        pc::println("No files found.");
        return;
    }

    constexpr uint32_t EXTENT_BYTES = 128u * 128u; // 16 384 bytes per extent

    pc::println("{:>4}  {:<12}  {:>10}  {:>10}", "User", "Name", "Size", "");
    pc::println("{:->4}  {:-<12}  {:->10}  {:->10}", "", "", "", "");

    for (const auto& [key, acc] : files) {
        auto [u, fname] = key;
        uint64_t size = uint64_t(acc.max_ext) * EXTENT_BYTES
                      + uint64_t(acc.last_rc) * 128u;
        pc::println("{:>4}  {:<12}  {:>10}  {:>10}",
            u, fname, size, human_size(size));
    }
    pc::println("");
    pc::println("{} file(s)", files.size());
}

// ── cmd_add ───────────────────────────────────────────────────────────────────

void cpm_disk::add_cpm_file(const std::array<char,8>& cpm_name,
                            const std::array<char,3>& cpm_ext,
                            std::span<const uint8_t> data,
                            int user) {
    uint64_t file_size = data.size();
    uint32_t bpe            = def_.blocks_per_extent();
    uint32_t blocks_needed  = uint32_t((file_size + def_.blocksize - 1) / def_.blocksize);
    uint32_t extents_needed = (blocks_needed == 0) ? 1u
                            : (blocks_needed + bpe - 1) / bpe;

    auto dir  = read_dir();
    auto used = used_blocks(dir);

    uint32_t free_blocks = def_.total_blocks() - uint32_t(used.size());
    if (blocks_needed > free_blocks)
        throw std::runtime_error(std::format(
            "need {} blocks, only {} free", blocks_needed, free_blocks));

    uint32_t free_entries = 0;
    for (const auto& e : dir) if (entry_is_free(e)) ++free_entries;
    if (extents_needed > free_entries)
        throw std::runtime_error(std::format(
            "need {} directory entries, only {} free", extents_needed, free_entries));

    std::vector<uint32_t> alloc(blocks_needed);
    for (auto& blk : alloc) blk = alloc_block(used);

    for (uint32_t i = 0; i < blocks_needed; ++i) {
        std::vector<uint8_t> buf(def_.blocksize, 0x1Au);
        uint64_t src_offset = uint64_t(i) * def_.blocksize;
        uint64_t copy_len   = std::min(uint64_t(def_.blocksize), file_size - src_offset);
        std::copy(data.begin() + std::ptrdiff_t(src_offset),
                  data.begin() + std::ptrdiff_t(src_offset + copy_len),
                  buf.begin());
        write_block(alloc[i], buf);
    }

    uint32_t ext_idx      = 0;
    uint32_t blocks_done  = 0;
    uint64_t bytes_left   = file_size;

    for (auto& e : dir) {
        if (!entry_is_free(e)) continue;
        if (ext_idx >= extents_needed) break;

        std::memset(&e, 0, sizeof(dir_entry));
        e.user = uint8_t(user);
        std::memcpy(e.name, cpm_name.data(), 8);
        std::memcpy(e.ext,  cpm_ext.data(),  3);
        e.xl = uint8_t(ext_idx & 0x1Fu);
        e.xh = uint8_t((ext_idx >> 5) & 0x3Fu);

        uint32_t blks_this = std::min(bpe, blocks_needed - blocks_done);
        if (def_.use_word_blocks()) {
            for (uint32_t i = 0; i < blks_this; ++i) {
                uint16_t bn = uint16_t(alloc[blocks_done + i]);
                e.al[i * 2] = uint8_t(bn & 0xFFu);
                e.al[i * 2 + 1] = uint8_t(bn >> 8);
            }
        } else {
            for (uint32_t i = 0; i < blks_this; ++i)
                e.al[i] = uint8_t(alloc[blocks_done + i] & 0xFFu);
        }

        uint64_t extent_bytes = std::min(uint64_t(blks_this) * def_.blocksize, bytes_left);
        e.rc = uint8_t((extent_bytes + 127u) / 128u);

        blocks_done += blks_this;
        bytes_left  -= extent_bytes;
        ++ext_idx;
    }

    write_dir(dir);
}

void cpm_disk::cmd_add(const std::filesystem::path& host_path, int user) {
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

    add_cpm_file(cpm_name, cpm_ext, data, user);

    // Build display name from the 8+3 arrays we computed earlier.
    std::string display_name = cpm_name_from_arrays(cpm_name, cpm_ext);
    uint32_t blocks_needed = uint32_t((file_size + def_.blocksize - 1) / def_.blocksize);

    pc::println("Added  {:>4}:{}  ({}, {} block(s))",
        user, display_name, human_size(file_size), blocks_needed);
}

// ── cmd_extract ───────────────────────────────────────────────────────────────

void cpm_disk::cmd_extract(const std::vector<std::string>& patterns,
                           const std::filesystem::path& out_dir,
                           int user) {
    if (user < -1 || user > 15)
        throw std::runtime_error(std::format("invalid user area {} (must be 0-15)", user));

    auto dir = read_dir();
    auto files = collect_files(dir, def_, user);

    std::vector<std::string> pats = patterns;
    if (pats.empty()) pats.push_back("*");
    for (auto& p : pats) p = to_upper(p);

    std::filesystem::create_directories(out_dir);
    size_t extracted = 0;

    for (const auto& f : files) {
        std::string up_name = to_upper(f.filename);
        bool match = false;
        for (const auto& p : pats) {
            if (wildcard_match(p, up_name)) {
                match = true;
                break;
            }
        }
        if (!match) continue;

        std::filesystem::path dst_dir = out_dir;
        if (user < 0) {
            dst_dir /= std::format("u{:02}", unsigned(f.user));
            std::filesystem::create_directories(dst_dir);
        }
        std::filesystem::path dst = dst_dir / f.filename;

        std::vector<uint8_t> data(f.size);
        uint64_t copied = 0;
        for (uint32_t blk : f.blocks) {
            if (copied >= f.size) break;
            auto block = read_block(blk);
            uint64_t n = std::min<uint64_t>(def_.blocksize, f.size - copied);
            std::copy(block.begin(), block.begin() + std::ptrdiff_t(n),
                      data.begin() + std::ptrdiff_t(copied));
            copied += n;
        }

        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
        if (!out)
            throw std::runtime_error(std::format("cannot create '{}'", dst.string()));
        if (!data.empty())
            out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!out)
            throw std::runtime_error(std::format("write failed for '{}'", dst.string()));

        pc::println("Extracted {:>4}:{} -> {}", unsigned(f.user), f.filename, dst.string());
        ++extracted;
    }

    if (extracted == 0)
        pc::println("No files matched.");
    else
        pc::println("Extracted {} file(s).", extracted);
}

// ── cmd_rename ────────────────────────────────────────────────────────────────

void cpm_disk::cmd_rename(const std::string& src_name,
                          const std::string& dst_name,
                          int user,
                          int to_user) {
    if (user < -1 || user > 15)
        throw std::runtime_error(std::format("invalid source user {} (must be 0-15)", user));
    if (to_user < -1 || to_user > 15)
        throw std::runtime_error(std::format("invalid destination user {} (must be 0-15)", to_user));

    auto [dst_cpm_name, dst_cpm_ext] = to_cpm_83(dst_name);
    std::string src_up = to_upper(src_name);
    std::string dst_disp = cpm_name_from_arrays(dst_cpm_name, dst_cpm_ext);
    std::string dst_up = to_upper(dst_disp);

    auto dir = read_dir();
    auto files = collect_files(dir, def_, user);

    std::vector<file_record> src_matches;
    for (const auto& f : files)
        if (to_upper(f.filename) == src_up) src_matches.push_back(f);

    if (src_matches.empty())
        throw std::runtime_error(std::format("source '{}' not found", src_name));
    if (src_matches.size() > 1)
        throw std::runtime_error(std::format("source '{}' is ambiguous; pass -u/--user", src_name));

    const auto& src = src_matches.front();
    uint8_t target_user = (to_user >= 0) ? uint8_t(to_user) : src.user;

    for (const auto& f : files) {
        if (f.user == target_user && to_upper(f.filename) == dst_up)
            throw std::runtime_error(std::format(
                "destination {:>4}:{} already exists", unsigned(target_user), dst_disp));
    }

    for (size_t idx : src.entry_indices) {
        auto& e = dir[idx];
        e.user = target_user;
        std::memcpy(e.name, dst_cpm_name.data(), 8);
        std::memcpy(e.ext,  dst_cpm_ext.data(),  3);
    }
    write_dir(dir);

    pc::println("Renamed {:>4}:{} -> {:>4}:{}",
        unsigned(src.user), src.filename, unsigned(target_user), dst_disp);
}

// ── cmd_copy ──────────────────────────────────────────────────────────────────

void cpm_disk::cmd_copy(const std::string& src_name,
                        const std::string& dst_name,
                        int user,
                        int to_user) {
    if (user < -1 || user > 15)
        throw std::runtime_error(std::format("invalid source user {} (must be 0-15)", user));
    if (to_user < -1 || to_user > 15)
        throw std::runtime_error(std::format("invalid destination user {} (must be 0-15)", to_user));

    auto [dst_cpm_name, dst_cpm_ext] = to_cpm_83(dst_name);
    std::string src_up = to_upper(src_name);
    std::string dst_disp = cpm_name_from_arrays(dst_cpm_name, dst_cpm_ext);
    std::string dst_up = to_upper(dst_disp);

    auto dir = read_dir();
    auto files = collect_files(dir, def_, user);

    std::vector<file_record> src_matches;
    for (const auto& f : files)
        if (to_upper(f.filename) == src_up) src_matches.push_back(f);

    if (src_matches.empty())
        throw std::runtime_error(std::format("source '{}' not found", src_name));
    if (src_matches.size() > 1)
        throw std::runtime_error(std::format("source '{}' is ambiguous; pass -u/--user", src_name));

    const auto& src = src_matches.front();
    uint8_t target_user = (to_user >= 0) ? uint8_t(to_user) : src.user;

    for (const auto& f : files) {
        if (f.user == target_user && to_upper(f.filename) == dst_up)
            throw std::runtime_error(std::format(
                "destination {:>4}:{} already exists", unsigned(target_user), dst_disp));
    }

    std::vector<uint8_t> data(src.size);
    uint64_t copied = 0;
    for (uint32_t blk : src.blocks) {
        if (copied >= src.size) break;
        auto block = read_block(blk);
        uint64_t n = std::min<uint64_t>(def_.blocksize, src.size - copied);
        std::copy(block.begin(), block.begin() + std::ptrdiff_t(n),
                  data.begin() + std::ptrdiff_t(copied));
        copied += n;
    }

    add_cpm_file(dst_cpm_name, dst_cpm_ext, data, target_user);
    pc::println("Copied  {:>4}:{} -> {:>4}:{}  ({})",
        unsigned(src.user), src.filename, unsigned(target_user), dst_disp, human_size(src.size));
}

// ── cmd_boot_* ────────────────────────────────────────────────────────────────

void cpm_disk::cmd_boot_read(const std::filesystem::path& out_path) {
    uint64_t boot_bytes = uint64_t(def_.boot_sectors()) * def_.seclen;
    if (boot_bytes == 0)
        throw std::runtime_error("this disk format has no reserved boot tracks");

    std::vector<uint8_t> data(boot_bytes);
    file_.seekg(0);
    file_.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
    if (!file_)
        throw std::runtime_error("failed to read boot/system track area");

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error(std::format("cannot create '{}'", out_path.string()));
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    if (!out)
        throw std::runtime_error(std::format("failed to write '{}'", out_path.string()));

    pc::println("Wrote boot/system area ({} bytes) to '{}'", boot_bytes, out_path.string());
}

void cpm_disk::cmd_boot_write(const std::filesystem::path& in_path) {
    uint64_t boot_bytes = uint64_t(def_.boot_sectors()) * def_.seclen;
    if (boot_bytes == 0)
        throw std::runtime_error("this disk format has no reserved boot tracks");

    if (!std::filesystem::exists(in_path))
        throw std::runtime_error(std::format("'{}': file not found", in_path.string()));

    uint64_t src_size = std::filesystem::file_size(in_path);
    if (src_size != boot_bytes)
        throw std::runtime_error(std::format(
            "'{}': expected {} bytes for boot/system area, got {}",
            in_path.string(), boot_bytes, src_size));

    std::vector<uint8_t> data(src_size);
    {
        std::ifstream in(in_path, std::ios::binary);
        if (!in)
            throw std::runtime_error(std::format("cannot open '{}' for reading", in_path.string()));
        in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
        if (!in)
            throw std::runtime_error(std::format("failed to read '{}'", in_path.string()));
    }

    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    file_.flush();
    if (!file_)
        throw std::runtime_error("failed to write boot/system track area");

    pc::println("Updated boot/system area ({} bytes) from '{}'", boot_bytes, in_path.string());
}

void cpm_disk::cmd_sysgen(const std::filesystem::path& in_path,
                          uint32_t offset_sectors,
                          bool keep_rest) {
    uint64_t boot_sectors = def_.boot_sectors();
    uint64_t boot_bytes = boot_sectors * def_.seclen;
    if (boot_bytes == 0)
        throw std::runtime_error("this disk format has no reserved boot tracks");

    if (offset_sectors >= boot_sectors)
        throw std::runtime_error(std::format(
            "offset sector {} is outside boot area ({} sectors)",
            offset_sectors, boot_sectors));

    if (!std::filesystem::exists(in_path))
        throw std::runtime_error(std::format("'{}': file not found", in_path.string()));

    uint64_t src_size = std::filesystem::file_size(in_path);
    uint64_t start = uint64_t(offset_sectors) * def_.seclen;
    uint64_t max_payload = boot_bytes - start;
    if (src_size > max_payload)
        throw std::runtime_error(std::format(
            "'{}': {} bytes do not fit boot area from sector {} (max {} bytes)",
            in_path.string(), src_size, offset_sectors, max_payload));

    std::vector<uint8_t> boot_img(boot_bytes, 0x00);
    if (keep_rest) {
        file_.seekg(0);
        file_.read(reinterpret_cast<char*>(boot_img.data()), std::streamsize(boot_img.size()));
        if (!file_)
            throw std::runtime_error("failed to read existing boot/system area");
    }

    std::vector<uint8_t> src(src_size);
    {
        std::ifstream in(in_path, std::ios::binary);
        if (!in)
            throw std::runtime_error(std::format("cannot open '{}' for reading", in_path.string()));
        if (!src.empty())
            in.read(reinterpret_cast<char*>(src.data()), std::streamsize(src.size()));
        if (!in && !src.empty())
            throw std::runtime_error(std::format("failed to read '{}'", in_path.string()));
    }

    if (!src.empty())
        std::copy(src.begin(), src.end(), boot_img.begin() + std::ptrdiff_t(start));

    file_.seekp(0);
    file_.write(reinterpret_cast<const char*>(boot_img.data()), std::streamsize(boot_img.size()));
    file_.flush();
    if (!file_)
        throw std::runtime_error("failed to write boot/system track area");

    pc::println("SYSGEN wrote {} bytes from '{}' at boot sector {} (boot area {} bytes, keep_rest={})",
        src_size, in_path.string(), offset_sectors, boot_bytes, keep_rest ? "yes" : "no");
}

// ── cmd_remove ────────────────────────────────────────────────────────────────

void cpm_disk::cmd_remove(const std::string& pattern, int user) {
    std::string up_pattern = to_upper(pattern);
    auto dir = read_dir();
    int removed = 0;

    for (auto& e : dir) {
        if (!entry_is_valid(e)) continue;
        if (user >= 0 && e.user != uint8_t(user)) continue;

        std::string fname = to_upper(entry_filename(e));
        if (wildcard_match(up_pattern, fname)) {
            pc::println("Removing {:>4}:{}", int(e.user), entry_filename(e));
            e.user = 0xE5;  // mark all extents of this file as deleted
            ++removed;
        }
    }

    if (removed == 0)
        pc::println("No files matched '{}'.", pattern);
    else {
        write_dir(dir);
        pc::println("Removed {} directory entr{}.", removed, removed == 1 ? "y" : "ies");
    }
}
