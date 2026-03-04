#include <cpmdisk/cpm_disk.h>
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
static disk_def resolve_create_def(const std::string& type,
                                   const geo_opts& geo,
                                   const std::string& diskdefs_file) {
    disk_def def;
    std::optional<std::filesystem::path> diskdefs_path;
    if (!diskdefs_file.empty())
        diskdefs_path = std::filesystem::path{diskdefs_file};

    if (!type.empty()) {
        auto opt = find_diskdef(type, diskdefs_path);
        if (!opt)
            throw std::runtime_error(std::format(
                "unknown disk type '{}' – use built-ins ('fdd','hdd') or a name from --diskdefs",
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
static std::optional<disk_def> resolve_open_hint(const std::string& fmt,
                                                 const geo_opts& geo,
                                                 const std::string& diskdefs_file) {
    if (fmt.empty() && !geo.any())
        return std::nullopt;

    disk_def def;
    std::optional<std::filesystem::path> diskdefs_path;
    if (!diskdefs_file.empty())
        diskdefs_path = std::filesystem::path{diskdefs_file};

    if (!fmt.empty()) {
        auto opt = find_diskdef(fmt, diskdefs_path);
        if (!opt)
            throw std::runtime_error(std::format(
                "unknown disk type '{}' – use built-ins ('fdd','hdd') or a name from --diskdefs",
                fmt));
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
    std::string create_diskdefs;
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
            "Base disk type (built-in fdd/hdd or from --diskdefs file)");
        cmd->add_option("--diskdefs", create_diskdefs,
            "Path to cpmtools diskdefs file used for type lookup");
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
    geo_opts    info_geo;
    std::string info_diskdefs;
    {
        auto* cmd = app.add_subcommand("info",
            "Show disk geometry and free-space statistics.");
        cmd->add_option("disk",        info_path, "Disk image file")->required();
        cmd->add_option("-f,--format", info_fmt,
            "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", info_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        add_geo_options(cmd, info_geo);
    }

    // ── list ──────────────────────────────────────────────────────────────────
    std::string list_path, list_fmt;
    int         list_user = -1;
    geo_opts    list_geo;
    std::string list_diskdefs;
    {
        auto* cmd = app.add_subcommand("list", "List files on the disk.");
        cmd->add_option("disk",        list_path, "Disk image file")->required();
        cmd->add_option("-f,--format", list_fmt,  "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", list_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        cmd->add_option("-u,--user",   list_user,
            "Restrict listing to a CP/M user area (0-15)");
        add_geo_options(cmd, list_geo);
    }

    // ── add ───────────────────────────────────────────────────────────────────
    std::string              add_path, add_fmt;
    int                      add_user = 0;
    std::vector<std::string> add_files;
    geo_opts                 add_geo;
    std::string              add_diskdefs;
    {
        auto* cmd = app.add_subcommand("add",
            "Add one or more host files to the disk.\n"
            "Shell wildcards are expanded by the shell before reaching this tool.\n"
            "Example: cpmdisk add boot.dsk -u 0 *.com *.bas");
        cmd->add_option("disk",        add_path,  "Disk image file")->required();
        cmd->add_option("-f,--format", add_fmt,   "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", add_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
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
    geo_opts                 rm_geo;
    std::string              rm_diskdefs;
    {
        auto* cmd = app.add_subcommand("remove",
            "Remove files matching one or more wildcard patterns.\n"
            "Patterns matched against CP/M NAME.EXT (case-insensitive).\n"
            "Wildcards: * = any sequence, ? = one character.\n"
            "Example: cpmdisk remove boot.dsk -u 0 '*.COM'");
        cmd->add_option("disk",        rm_path,     "Disk image file")->required();
        cmd->add_option("-f,--format", rm_fmt,      "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", rm_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        cmd->add_option("-u,--user",   rm_user,
            "Restrict removal to a CP/M user area (0-15, default: all)");
        cmd->add_option("patterns", rm_patterns,
            "Wildcard patterns matched against NAME.EXT")
           ->required()->expected(-1);
        add_geo_options(cmd, rm_geo);
    }

    // ── extract ───────────────────────────────────────────────────────────────
    std::string              ex_path, ex_fmt;
    int                      ex_user = -1;
    std::vector<std::string> ex_patterns;
    std::string              ex_outdir = ".";
    geo_opts                 ex_geo;
    std::string              ex_diskdefs;
    {
        auto* cmd = app.add_subcommand("extract",
            "Extract files from disk image to host directory.\n"
            "If no patterns are given, all files are extracted.");
        cmd->add_option("disk",        ex_path, "Disk image file")->required();
        cmd->add_option("-f,--format", ex_fmt,  "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", ex_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        cmd->add_option("-u,--user",   ex_user,
            "Restrict extraction to a CP/M user area (0-15)");
        cmd->add_option("-o,--outdir", ex_outdir,
            "Host output directory (default: current directory)");
        cmd->add_option("patterns", ex_patterns,
            "Wildcard patterns matched against NAME.EXT")
           ->expected(-1);
        add_geo_options(cmd, ex_geo);
    }

    // ── rename ────────────────────────────────────────────────────────────────
    std::string rn_path, rn_fmt, rn_src, rn_dst;
    int         rn_user = -1;
    int         rn_to_user = -1;
    geo_opts    rn_geo;
    std::string rn_diskdefs;
    {
        auto* cmd = app.add_subcommand("rename",
            "Rename a file within the image.");
        cmd->add_option("disk", rn_path, "Disk image file")->required();
        cmd->add_option("src", rn_src, "Source CP/M filename")->required();
        cmd->add_option("dst", rn_dst, "Destination CP/M filename")->required();
        cmd->add_option("-f,--format", rn_fmt, "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", rn_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        cmd->add_option("-u,--user", rn_user,
            "Source user area (0-15). Required when name exists in multiple users");
        cmd->add_option("--to-user", rn_to_user,
            "Destination user area (0-15, default: source user)");
        add_geo_options(cmd, rn_geo);
    }

    // ── copy ──────────────────────────────────────────────────────────────────
    std::string cp_path, cp_fmt, cp_src, cp_dst;
    int         cp_user = -1;
    int         cp_to_user = -1;
    geo_opts    cp_geo;
    std::string cp_diskdefs;
    {
        auto* cmd = app.add_subcommand("copy",
            "Copy a file within the image.");
        cmd->add_option("disk", cp_path, "Disk image file")->required();
        cmd->add_option("src", cp_src, "Source CP/M filename")->required();
        cmd->add_option("dst", cp_dst, "Destination CP/M filename")->required();
        cmd->add_option("-f,--format", cp_fmt, "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", cp_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        cmd->add_option("-u,--user", cp_user,
            "Source user area (0-15). Required when name exists in multiple users");
        cmd->add_option("--to-user", cp_to_user,
            "Destination user area (0-15, default: source user)");
        add_geo_options(cmd, cp_geo);
    }

    // ── bootread / bootwrite ──────────────────────────────────────────────────
    std::string br_path, br_fmt, br_out;
    geo_opts    br_geo;
    std::string br_diskdefs;
    {
        auto* cmd = app.add_subcommand("bootread",
            "Read reserved boot/system track area to a host file.");
        cmd->add_option("disk", br_path, "Disk image file")->required();
        cmd->add_option("out", br_out, "Output host binary file")->required();
        cmd->add_option("-f,--format", br_fmt, "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", br_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        add_geo_options(cmd, br_geo);
    }

    std::string bw_path, bw_fmt, bw_in;
    geo_opts    bw_geo;
    std::string bw_diskdefs;
    {
        auto* cmd = app.add_subcommand("bootwrite",
            "Write reserved boot/system track area from a host file.");
        cmd->add_option("disk", bw_path, "Disk image file")->required();
        cmd->add_option("in", bw_in, "Input host binary file")->required();
        cmd->add_option("-f,--format", bw_fmt, "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", bw_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        add_geo_options(cmd, bw_geo);
    }

    // ── sysgen ────────────────────────────────────────────────────────────────
    std::string sg_path, sg_fmt, sg_in;
    uint32_t    sg_offset_sectors = 0;
    bool        sg_keep_rest = false;
    geo_opts    sg_geo;
    std::string sg_diskdefs;
    {
        auto* cmd = app.add_subcommand("sysgen",
            "Write a CP/M system image into reserved boot/system tracks.\n"
            "By default, writes at sector 0 and clears the rest of boot area.");
        cmd->add_option("disk", sg_path, "Disk image file")->required();
        cmd->add_option("sys", sg_in, "Host CP/M system image binary")->required();
        cmd->add_option("-f,--format", sg_fmt, "Force disk type by name (built-in or from --diskdefs)");
        cmd->add_option("--diskdefs", sg_diskdefs,
            "Path to cpmtools diskdefs file used for -f/--format lookup");
        cmd->add_option("--offset-sectors", sg_offset_sectors,
            "Start writing at this sector within reserved boot area");
        cmd->add_flag("--keep-rest", sg_keep_rest,
            "Keep existing bytes outside written system image region");
        add_geo_options(cmd, sg_geo);
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
            disk_def def = resolve_create_def(create_type, create_geo, create_diskdefs);
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
            auto disk = cpm_disk::open({info_path},
                                       resolve_open_hint(info_fmt, info_geo, info_diskdefs));
            disk.cmd_info();
        }

        // ── list ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("list")) {
            auto disk = cpm_disk::open({list_path},
                                       resolve_open_hint(list_fmt, list_geo, list_diskdefs));
            disk.cmd_list(list_user);
        }

        // ── add ───────────────────────────────────────────────────────────────
        else if (app.got_subcommand("add")) {
            auto disk = cpm_disk::open({add_path},
                                       resolve_open_hint(add_fmt, add_geo, add_diskdefs));
            for (const auto& f : add_files)
                disk.cmd_add(std::filesystem::path{f}, add_user);
        }

        // ── remove ────────────────────────────────────────────────────────────
        else if (app.got_subcommand("remove")) {
            auto disk = cpm_disk::open({rm_path},
                                       resolve_open_hint(rm_fmt, rm_geo, rm_diskdefs));
            for (const auto& pat : rm_patterns)
                disk.cmd_remove(pat, rm_user);
        }

        // ── extract ───────────────────────────────────────────────────────────
        else if (app.got_subcommand("extract")) {
            auto disk = cpm_disk::open({ex_path},
                                       resolve_open_hint(ex_fmt, ex_geo, ex_diskdefs));
            disk.cmd_extract(ex_patterns, std::filesystem::path{ex_outdir}, ex_user);
        }

        // ── rename ────────────────────────────────────────────────────────────
        else if (app.got_subcommand("rename")) {
            auto disk = cpm_disk::open({rn_path},
                                       resolve_open_hint(rn_fmt, rn_geo, rn_diskdefs));
            disk.cmd_rename(rn_src, rn_dst, rn_user, rn_to_user);
        }

        // ── copy ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("copy")) {
            auto disk = cpm_disk::open({cp_path},
                                       resolve_open_hint(cp_fmt, cp_geo, cp_diskdefs));
            disk.cmd_copy(cp_src, cp_dst, cp_user, cp_to_user);
        }

        // ── bootread ──────────────────────────────────────────────────────────
        else if (app.got_subcommand("bootread")) {
            auto disk = cpm_disk::open({br_path},
                                       resolve_open_hint(br_fmt, br_geo, br_diskdefs));
            disk.cmd_boot_read(std::filesystem::path{br_out});
        }

        // ── bootwrite ─────────────────────────────────────────────────────────
        else if (app.got_subcommand("bootwrite")) {
            auto disk = cpm_disk::open({bw_path},
                                       resolve_open_hint(bw_fmt, bw_geo, bw_diskdefs));
            disk.cmd_boot_write(std::filesystem::path{bw_in});
        }

        // ── sysgen ────────────────────────────────────────────────────────────
        else if (app.got_subcommand("sysgen")) {
            auto disk = cpm_disk::open({sg_path},
                                       resolve_open_hint(sg_fmt, sg_geo, sg_diskdefs));
            disk.cmd_sysgen(std::filesystem::path{sg_in}, sg_offset_sectors, sg_keep_rest);
        }

    } catch (const std::exception& ex) {
        pc::println(stderr, "error: {}", ex.what());
        return 1;
    }

    return 0;
}
