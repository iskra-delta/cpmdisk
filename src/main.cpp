#include "cpm_disk.h"
#include "print_compat.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

// Register all seven geometry override options on `cmd` under a named group.
static void add_geo_options(CLI::App* cmd, GeoOpts& g) {
    auto* grp = cmd->add_option_group("Geometry overrides",
        "Fine-tune or fully specify disk geometry (applied on top of the named type).");
    grp->add_option("--seclen",    g.seclen,    "Sector size in bytes");
    grp->add_option("--tracks",    g.tracks,    "Total number of tracks");
    grp->add_option("--sectrk",    g.sectrk,    "Sectors per track");
    grp->add_option("--blocksize", g.blocksize, "Allocation block size in bytes");
    grp->add_option("--maxdir",    g.maxdir,    "Maximum directory entries");
    grp->add_option("--skew",      g.skew,      "Sector skew factor (default 0)");
    grp->add_option("--boottrk",   g.boottrk,   "Number of reserved boot tracks");
}

// Resolve the DiskDef for create.
// `type` may be empty only when geo supplies all required fields.
static DiskDef resolve_create_def(const std::string& type, const GeoOpts& geo) {
    DiskDef def;
    if (!type.empty()) {
        auto opt = find_diskdef(type);
        if (!opt)
            throw std::runtime_error(std::format(
                "unknown disk type '{}' – use 'fdd', 'hdd', or omit and supply geometry flags",
                type));
        def = *opt;
    } else if (geo.all_required()) {
        def = DiskDef{"custom", 256, 0, 0, 1024, 64, 0, 0};
    } else {
        throw std::runtime_error(
            "create: specify a disk type (fdd/hdd) or supply all geometry flags\n"
            "  Required: --seclen --tracks --sectrk --blocksize --maxdir --boottrk");
    }
    geo.apply_to(def);
    return def;
}

// Resolve the optional DiskDef hint for open commands.
// Returns nullopt to trigger size-based auto-detection when nothing is specified.
static std::optional<DiskDef> resolve_open_hint(const std::string& fmt, const GeoOpts& geo) {
    if (fmt.empty() && !geo.any())
        return std::nullopt;

    DiskDef def;
    if (!fmt.empty()) {
        auto opt = find_diskdef(fmt);
        if (!opt)
            throw std::runtime_error(std::format("unknown disk type '{}'", fmt));
        def = *opt;
    } else {
        if (!geo.all_required())
            throw std::runtime_error(
                "geometry flags given but incomplete – required: "
                "--seclen --tracks --sectrk --blocksize --maxdir --boottrk");
        def = DiskDef{"custom", 256, 0, 0, 1024, 64, 0, 0};
    }
    geo.apply_to(def);
    return def;
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {

    CLI::App app{
        "cpmdisk - Iskra Delta Partner CP/M disk image tool\n"
        "\n"
        "Named disk types:\n"
        "  fdd   146 tracks, 18 sec/trk, 2048-byte blocks  (655 KB)\n"
        "  hdd   1224 tracks, 32 sec/trk, 4096-byte blocks (9.6 MB)\n"
        "\n"
        "Any parameter can be overridden with geometry flags.\n"
        "Omit the type entirely and supply all flags for a fully custom disk.\n"
    };
    app.require_subcommand(1);
    app.set_help_all_flag("--help-all", "Show help for all sub-commands");

    // ── create ────────────────────────────────────────────────────────────────
    std::string create_path, create_type;
    GeoOpts     create_geo;
    {
        auto* cmd = app.add_subcommand("create",
            "Create a new blank disk image.\n"
            "  cpmdisk create boot.dsk fdd\n"
            "  cpmdisk create boot.dsk hdd --maxdir 512\n"
            "  cpmdisk create boot.dsk --seclen 256 --tracks 80 --sectrk 9 "
                "--blocksize 2048 --maxdir 64 --boottrk 2");
        cmd->add_option("disk", create_path, "Output .dsk file path")->required();
        cmd->add_option("type", create_type,
            "Base disk type: fdd or hdd (optional when all geometry flags are given)");
        add_geo_options(cmd, create_geo);
    }

    // ── info ──────────────────────────────────────────────────────────────────
    std::string info_path, info_fmt;
    GeoOpts     info_geo;
    {
        auto* cmd = app.add_subcommand("info",
            "Show disk geometry and free-space statistics.");
        cmd->add_option("disk",        info_path, "Disk image file")->required();
        cmd->add_option("-f,--format", info_fmt,
            "Force disk type: fdd or hdd (overrides size-based auto-detection)");
        add_geo_options(cmd, info_geo);
    }

    // ── list ──────────────────────────────────────────────────────────────────
    std::string list_path, list_fmt;
    int         list_user = -1;
    GeoOpts     list_geo;
    {
        auto* cmd = app.add_subcommand("list", "List files on the disk.");
        cmd->add_option("disk",        list_path, "Disk image file")->required();
        cmd->add_option("-f,--format", list_fmt,  "Force disk type: fdd or hdd");
        cmd->add_option("-u,--user",   list_user,
            "Restrict listing to a CP/M user area (0-15)");
        add_geo_options(cmd, list_geo);
    }

    // ── add ───────────────────────────────────────────────────────────────────
    std::string              add_path, add_fmt;
    int                      add_user = 0;
    std::vector<std::string> add_files;
    GeoOpts                  add_geo;
    {
        auto* cmd = app.add_subcommand("add",
            "Add one or more host files to the disk.\n"
            "Shell wildcards are expanded by the shell before reaching this tool.\n"
            "Example: cpmdisk add boot.dsk -u 0 *.com *.bas");
        cmd->add_option("disk",        add_path,  "Disk image file")->required();
        cmd->add_option("-f,--format", add_fmt,   "Force disk type: fdd or hdd");
        cmd->add_option("-u,--user",   add_user,
            "Target CP/M user area (0-15, default 0)");
        cmd->add_option("files", add_files, "Host files to copy onto the disk")
           ->required()->expected(-1);
        add_geo_options(cmd, add_geo);
    }

    // ── remove ────────────────────────────────────────────────────────────────
    std::string              rm_path, rm_fmt;
    int                      rm_user = -1;
    std::vector<std::string> rm_patterns;
    GeoOpts                  rm_geo;
    {
        auto* cmd = app.add_subcommand("remove",
            "Remove files matching one or more wildcard patterns.\n"
            "Patterns matched against CP/M NAME.EXT (case-insensitive).\n"
            "Wildcards: * = any sequence, ? = one character.\n"
            "Example: cpmdisk remove boot.dsk -u 0 '*.COM'");
        cmd->add_option("disk",        rm_path,     "Disk image file")->required();
        cmd->add_option("-f,--format", rm_fmt,      "Force disk type: fdd or hdd");
        cmd->add_option("-u,--user",   rm_user,
            "Restrict removal to a CP/M user area (0-15, default: all)");
        cmd->add_option("patterns", rm_patterns,
            "Wildcard patterns matched against NAME.EXT")
           ->required()->expected(-1);
        add_geo_options(cmd, rm_geo);
    }

    CLI11_PARSE(app, argc, argv);

    try {
        // ── create ────────────────────────────────────────────────────────────
        if (app.got_subcommand("create")) {
            std::filesystem::path p{create_path};
            if (std::filesystem::exists(p))
                throw std::runtime_error(std::format(
                    "'{}' already exists; remove it first", create_path));

            DiskDef def = resolve_create_def(create_type, create_geo);
            CpmDisk::create(p, def);
            println("Created {} image '{}': {} tracks x {} sec/trk x {} B = {} bytes",
                def.name, create_path,
                def.tracks, def.sectrk, def.seclen,
                def.disk_size());
        }

        // ── info ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("info")) {
            auto disk = CpmDisk::open({info_path}, resolve_open_hint(info_fmt, info_geo));
            disk.cmd_info();
        }

        // ── list ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("list")) {
            auto disk = CpmDisk::open({list_path}, resolve_open_hint(list_fmt, list_geo));
            disk.cmd_list(list_user);
        }

        // ── add ───────────────────────────────────────────────────────────────
        else if (app.got_subcommand("add")) {
            auto disk = CpmDisk::open({add_path}, resolve_open_hint(add_fmt, add_geo));
            for (const auto& f : add_files)
                disk.cmd_add(std::filesystem::path{f}, add_user);
        }

        // ── remove ────────────────────────────────────────────────────────────
        else if (app.got_subcommand("remove")) {
            auto disk = CpmDisk::open({rm_path}, resolve_open_hint(rm_fmt, rm_geo));
            for (const auto& pat : rm_patterns)
                disk.cmd_remove(pat, rm_user);
        }

    } catch (const std::exception& ex) {
        println(stderr, "error: {}", ex.what());
        return 1;
    }

    return 0;
}
