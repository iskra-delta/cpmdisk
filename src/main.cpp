#include "cpm_disk.h"
#include "print_compat.h"

#include <CLI/CLI.hpp>
#include <filesystem>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {

    CLI::App app{
        "idpdisk - Iskra Delta Partner CP/M disk image tool\n"
        "\n"
        "Disk types:\n"
        "  fdd   146 tracks, 18 sec/trk, 2048-byte blocks  (655 KB)\n"
        "  hdd   1224 tracks, 32 sec/trk, 4096-byte blocks (9.6 MB)\n"
    };
    app.require_subcommand(1);
    app.set_help_all_flag("--help-all", "Show help for all sub-commands");

    // ── create ────────────────────────────────────────────────────────────────
    std::string create_path, create_type;
    {
        auto* cmd = app.add_subcommand("create", "Create a new blank disk image");
        cmd->add_option("disk", create_path, "Output .dsk file path")->required();
        cmd->add_option("type", create_type, "Disk type: fdd or hdd")->required();
    }

    // ── info ──────────────────────────────────────────────────────────────────
    std::string info_path;
    {
        auto* cmd = app.add_subcommand("info", "Show disk geometry and free-space statistics");
        cmd->add_option("disk", info_path, "Disk image file")->required();
    }

    // ── list ──────────────────────────────────────────────────────────────────
    std::string list_path;
    int         list_user = -1;
    {
        auto* cmd = app.add_subcommand("list", "List files on the disk");
        cmd->add_option("disk", list_path, "Disk image file")->required();
        cmd->add_option("-u,--user", list_user,
            "Restrict listing to a CP/M user area (0-15)");
    }

    // ── add ───────────────────────────────────────────────────────────────────
    std::string              add_path;
    int                      add_user = 0;
    std::vector<std::string> add_files;
    {
        auto* cmd = app.add_subcommand("add",
            "Add one or more host files to the disk.\n"
            "Shell wildcards are expanded by the shell before reaching this tool.\n"
            "Example: idpdisk add boot.dsk -u 0 *.com *.bas");
        cmd->add_option("disk", add_path, "Disk image file")->required();
        cmd->add_option("-u,--user", add_user,
            "Target CP/M user area (0-15, default 0)");
        cmd->add_option("files", add_files, "Host files to copy onto the disk")
           ->required()
           ->expected(-1);
    }

    // ── remove ────────────────────────────────────────────────────────────────
    std::string              rm_path;
    int                      rm_user = -1;
    std::vector<std::string> rm_patterns;
    {
        auto* cmd = app.add_subcommand("remove",
            "Remove files matching one or more wildcard patterns.\n"
            "Patterns matched against CP/M NAME.EXT (case-insensitive).\n"
            "Wildcards: * = any sequence, ? = one character.\n"
            "Example: idpdisk remove boot.dsk -u 0 '*.COM'");
        cmd->add_option("disk", rm_path, "Disk image file")->required();
        cmd->add_option("-u,--user", rm_user,
            "Restrict removal to a CP/M user area (0-15, default: all)");
        cmd->add_option("patterns", rm_patterns,
            "Wildcard patterns matched against NAME.EXT")
           ->required()
           ->expected(-1);
    }

    CLI11_PARSE(app, argc, argv);

    try {
        // ── create ────────────────────────────────────────────────────────────
        if (app.got_subcommand("create")) {
            auto def_opt = find_diskdef(create_type);
            if (!def_opt)
                throw std::runtime_error(std::format(
                    "unknown disk type '{}' – use 'fdd' or 'hdd'", create_type));

            std::filesystem::path p{create_path};
            if (std::filesystem::exists(p))
                throw std::runtime_error(std::format(
                    "'{}' already exists; remove it first", create_path));

            CpmDisk::create(p, *def_opt);
            const auto& d = *def_opt;
            println("Created {} image '{}': {} tracks x {} sec/trk x {} B = {} bytes",
                d.name, create_path,
                d.tracks, d.sectrk, d.seclen,
                d.disk_size());
        }

        // ── info ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("info")) {
            auto disk = CpmDisk::open(std::filesystem::path{info_path});
            disk.cmd_info();
        }

        // ── list ──────────────────────────────────────────────────────────────
        else if (app.got_subcommand("list")) {
            auto disk = CpmDisk::open(std::filesystem::path{list_path});
            disk.cmd_list(list_user);
        }

        // ── add ───────────────────────────────────────────────────────────────
        else if (app.got_subcommand("add")) {
            auto disk = CpmDisk::open(std::filesystem::path{add_path});
            for (const auto& f : add_files)
                disk.cmd_add(std::filesystem::path{f}, add_user);
        }

        // ── remove ────────────────────────────────────────────────────────────
        else if (app.got_subcommand("remove")) {
            auto disk = CpmDisk::open(std::filesystem::path{rm_path});
            for (const auto& pat : rm_patterns)
                disk.cmd_remove(pat, rm_user);
        }

    } catch (const std::exception& ex) {
        println(stderr, "error: {}", ex.what());
        return 1;
    }

    return 0;
}
