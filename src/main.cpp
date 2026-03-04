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
static void add_geo_options(CLI::App* cmd, geo_opts& g) {
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

// Resolve the disk_def for create.
// `type` may be empty only when geo supplies all required fields.
static disk_def resolve_create_def(const std::string& type, const geo_opts& geo) {
    disk_def def;
    if (!type.empty()) {
        auto opt = find_diskdef(type);
        if (!opt)
            throw std::runtime_error(std::format(
                "unknown disk type '{}' – use 'fdd', 'hdd', or omit and supply geometry flags",
                type));
        def = *opt;
    } else if (geo.all_required()) {
        def = disk_def{"custom", 256, 0, 0, 1024, 64, 0, 0};
    } else {
        throw std::runtime_error(
            "create: specify a disk type (fdd/hdd) or supply all geometry flags\n"
            "  Required: --seclen --tracks --sectrk --blocksize --maxdir --boottrk");
    }
    geo.apply_to(def);
    return def;
}

static bool is_partner_type(std::string_view type) {
    return type == "fdd" || type == "hdd" || type == "idpfdd" || type == "idphdd";
}

// Resolve the optional disk_def hint for open commands.
// Returns nullopt to trigger size-based auto-detection when nothing is specified.
static std::optional<disk_def> resolve_open_hint(const std::string& fmt, const geo_opts& geo) {
    if (fmt.empty() && !geo.any())
        return std::nullopt;

    disk_def def;
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
        def = disk_def{"custom", 256, 0, 0, 1024, 64, 0, 0};
    }
    geo.apply_to(def);
    return def;
}

static create_opts resolve_create_opts(bool cpm3, const std::string& label, bool datestamp) {
    if (!cpm3 && (!label.empty() || datestamp))
        throw std::runtime_error(
            "create: --label and --datestamp are only valid in CP/M 3 mode "
            "(partner type by default, or pass --cpm3)");

    return create_opts{
        .cpm3 = cpm3,
        .label = label,
        .datestamp = datestamp
    };
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
    geo_opts    create_geo;
    bool        create_cpm3 = false;
    std::string create_label;
    bool        create_datestamp = false;
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
        cmd->add_flag("--cpm3", create_cpm3,
            "Enable CP/M 3 mode (needed for custom geometry; partner types default to CP/M 3)");
        cmd->add_option("--label", create_label,
            "CP/M 3 disk label in 8.3 form (for example PARTNER or DATA.DISK)");
        cmd->add_flag("--datestamp", create_datestamp,
            "CP/M 3: reserve directory datestamp metadata marker");
        add_geo_options(cmd, create_geo);
    }

    // ── info ──────────────────────────────────────────────────────────────────
    std::string info_path, info_fmt;
    geo_opts     info_geo;
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
    geo_opts     list_geo;
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
    geo_opts                  add_geo;
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
    geo_opts                  rm_geo;
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

            bool partner_default_cpm3 = is_partner_type(create_type);
            disk_def def = resolve_create_def(create_type, create_geo);
            create_opts opts = resolve_create_opts(partner_default_cpm3 || create_cpm3,
                                                   create_label, create_datestamp);
            cpm_disk::create(p, def, opts);
            pc::println("Created {} image '{}': CP/M {}, {} tracks x {} sec/trk x {} B = {} bytes",
                def.name, create_path,
                opts.cpm3 ? 3 : 2,
                def.tracks, def.sectrk, def.seclen,
                def.disk_size());
        }

        // ── info ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("info")) {
            auto disk = cpm_disk::open({info_path}, resolve_open_hint(info_fmt, info_geo));
            disk.cmd_info();
        }

        // ── list ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("list")) {
            auto disk = cpm_disk::open({list_path}, resolve_open_hint(list_fmt, list_geo));
            disk.cmd_list(list_user);
        }

        // ── add ───────────────────────────────────────────────────────────────
        else if (app.got_subcommand("add")) {
            auto disk = cpm_disk::open({add_path}, resolve_open_hint(add_fmt, add_geo));
            for (const auto& f : add_files)
                disk.cmd_add(std::filesystem::path{f}, add_user);
        }

        // ── remove ────────────────────────────────────────────────────────────
        else if (app.got_subcommand("remove")) {
            auto disk = cpm_disk::open({rm_path}, resolve_open_hint(rm_fmt, rm_geo));
            for (const auto& pat : rm_patterns)
                disk.cmd_remove(pat, rm_user);
        }

    } catch (const std::exception& ex) {
        pc::println(stderr, "error: {}", ex.what());
        return 1;
    }

    return 0;
}
