[![status.badge]][status.url] [![language.badge]][language.url] [![standard.badge]][standard.url] [![license.badge]][license.url]

# cpmdisk

`cpmdisk` is a command-line tool for creating and managing CP/M disk images used by the Iskra Delta Partner.

It supports the Partner's known floppy and hard disk geometries and can also work with fully custom geometries when all required parameters are supplied.

## Architecture

This project is intentionally split into two parts:

- Frontend: `cpmdisk` CLI in `src/main.cpp` (argument parsing and UX).
- Backend: shared CP/M library target `cpmdisk_lib` (artifact: `libcpmdisk.*` / `cpmdisk.dll`) in `lib/`, with public headers in `include/cpm/`.

You can freely reuse the backend library in your own CP/M project without using this CLI.
The frontend in this repository is the reference consumer of that backend API, so practical backend usage examples are in `src/main.cpp`.

## Current scope

Implemented commands:

- `create`: create a blank disk image
- `info`: show geometry and free-space statistics
- `list`: list files (optionally by CP/M user area)
- `add`: copy host files into a CP/M user area
- `extract`: copy files from image to host
- `rename`: rename file inside image (optionally move between user areas)
- `copy`: copy file inside image (optionally to another user area)
- `remove`: delete by wildcard pattern (`*`, `?`)
- `bootread`: export reserved boot/system tracks
- `bootwrite`: import reserved boot/system tracks
- `sysgen`: place CP/M system image into boot/system tracks

## Disk formats

Built-in named formats:

| Type | Internal name | Tracks | Sec/trk | Sector size | Block size | Max dir | Boot trk | Size |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `fdd` | `idpfdd` | 146 | 18 | 256 | 2048 | 128 | 2 | 672,768 B |
| `hdd` | `idphdd` | 1224 | 32 | 256 | 4096 | 1024 | 1 | 10,027,008 B |

## Requirements

- CMake `>= 3.26`
- C++23 compiler (`std::format` required; tested with GCC 13+)
- `git` (used by CMake `FetchContent` to fetch CLI11)

Dependency fetched automatically:

- [CLI11](https://github.com/CLIUtils/CLI11) `v2.4.2`

## Build

```bash
cmake -B build
cmake --build build -j4
./bin/cpmdisk --help
```

This builds:

- `cpmdisk_lib` shared library target (artifact name: `libcpmdisk.*` / `cpmdisk.dll`)
- `cpmdisk` CLI executable (argument parsing/front-end)

Release build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

## Using The Library

Public headers:

- `include/cpm/cpm_disk.h`
- `include/cpm/diskdef.h`
- `include/cpm/direntry.h`

In CMake-based projects, link against `cpmdisk_lib` and include `cpm/...` headers.
For concrete call flows, see `src/main.cpp`: each CLI subcommand maps directly to backend library calls.

## CLI overview

```text
cpmdisk <command> <disk> [options]
```

`disk` is always the `.dsk` image path.  
Most commands also accept either:

- automatic geometry detection (default), or
- explicit geometry/type hints via `-f/--format`, `--diskdefs`, or geometry flags.

## Parameter quick reference

Common options (many commands):

- `-f, --format <name>`: force a named geometry (`fdd`, `hdd`, or a name from `--diskdefs`)
- `--diskdefs <file>`: load additional named formats from a cpmtools `diskdefs` file
- `--seclen`, `--tracks`, `--sectrk`, `--blocksize`, `--maxdir`, `--skew`, `--boottrk`:
  explicit geometry override fields

File/user options:

- `-u, --user <0..15>`: source/filter CP/M user area
- `--to-user <0..15>`: destination user area for `copy`/`rename`
- `-o, --outdir <dir>`: output directory for `extract`

CP/M 3 create options:

- `--cpm3`: enable CP/M 3 mode for custom geometry (`fdd`/`hdd` already default to CP/M 3)
- `--label <8.3>`: add CP/M 3 disk label marker entry
- `--datestamp`: add CP/M 3 datestamp marker entry

### create

Create a new blank disk image.

```bash
cpmdisk create boot.dsk fdd
cpmdisk create hdd.dsk hdd
```

Create CP/M 3 directory markers:

```bash
cpmdisk create boot3.dsk fdd --label PARTNER --datestamp
```

Enable CP/M 3 on custom geometry:

```bash
cpmdisk create custom3.dsk --cpm3 \
  --seclen 256 --tracks 80 --sectrk 9 \
  --blocksize 2048 --maxdir 64 --boottrk 2
```

Custom geometry (type omitted):

```bash
cpmdisk create custom.dsk \
  --seclen 256 --tracks 80 --sectrk 9 \
  --blocksize 2048 --maxdir 64 --boottrk 2
```

Use cpmtools `diskdefs` as an alternative to manual geometry:

```bash
cpmdisk create from-def.dsk myformat --diskdefs ./diskdefs
cpmdisk info from-def.dsk -f myformat --diskdefs ./diskdefs
```

### info

Show geometry and allocation statistics.

```bash
cpmdisk info boot.dsk
```

If size auto-detection cannot identify the image, provide format or full geometry:

```bash
cpmdisk info custom.dsk -f fdd
cpmdisk info custom.dsk --seclen 256 --tracks 80 --sectrk 9 --blocksize 2048 --maxdir 64 --boottrk 2
```

### list

List files from all user areas, or a specific user area (`0-15`):

```bash
cpmdisk list boot.dsk
cpmdisk list boot.dsk -u 0
```

### add

Copy one or more host files into the disk.

```bash
cpmdisk add boot.dsk COMMAND.COM
cpmdisk add boot.dsk -u 3 MYPROG.COM DATA.DAT
```

### extract

Extract by wildcard, optionally limiting user and output directory:

```bash
cpmdisk extract boot.dsk '*.COM'
cpmdisk extract boot.dsk -u 0 -o out '*.TXT'
```

### rename / copy

Rename or copy inside image:

```bash
cpmdisk rename boot.dsk OLD.COM NEW.COM -u 0
cpmdisk copy boot.dsk APP.COM APP2.COM -u 0 --to-user 3
```

### remove

Delete files by wildcard pattern (`*`, `?`), optionally restricted to one user area:

```bash
cpmdisk remove boot.dsk '*.BAK'
cpmdisk remove boot.dsk -u 0 '*.COM'
```

### bootread / bootwrite

Export or import boot/system track area:

```bash
cpmdisk bootread boot.dsk boot.bin
cpmdisk bootwrite boot.dsk boot.bin
```

### sysgen

Write CP/M system image into reserved boot/system tracks:

```bash
cpmdisk sysgen boot.dsk CPM3.SYS
cpmdisk sysgen boot.dsk CPM3.SYS --offset-sectors 1 --keep-rest
```

Meaning of `sysgen` options:

- `sys`: raw binary payload to write into reserved boot area
- `--offset-sectors N`: start writing at boot-area sector `N` (default `0`)
- `--keep-rest`: preserve existing boot-area bytes outside written range
  without it, the whole boot area is zero-filled first

`sysgen` validates that payload fits inside reserved boot tracks (`boottrk * sectrk * seclen`).

## Typical workflows

Create bootable-style image (Partner defaults):

```bash
cpmdisk create boot.dsk fdd
cpmdisk sysgen boot.dsk CPM3.SYS
cpmdisk add boot.dsk COMMAND.COM
```

Patch existing boot area without touching other sectors:

```bash
cpmdisk bootread boot.dsk boot-before.bin
cpmdisk sysgen boot.dsk CPM3.SYS --offset-sectors 1 --keep-rest
cpmdisk bootread boot.dsk boot-after.bin
```

Import/export files:

```bash
cpmdisk add boot.dsk -u 0 APP.COM
cpmdisk extract boot.dsk -u 0 -o out 'APP.*'
```

In-image file operations:

```bash
cpmdisk copy boot.dsk APP.COM APP2.COM -u 0
cpmdisk rename boot.dsk APP2.COM APPX.COM -u 0 --to-user 3
```

## Geometry override options

All commands support geometry overrides:

- `--seclen`
- `--tracks`
- `--sectrk`
- `--blocksize`
- `--maxdir`
- `--skew`
- `--boottrk`
- `--diskdefs` (for named format lookup from a cpmtools `diskdefs` file)

Rules:

- For `create`: provide either a named type (`fdd`/`hdd`) or all required geometry fields.
- Partner types (`fdd`, `hdd`, `idpfdd`, `idphdd`) default to CP/M 3 mode.
- Custom geometry defaults to CP/M 2.2 mode; pass `--cpm3` to enable CP/M 3 mode.
- `--label` and `--datestamp` require CP/M 3 mode and add CP/M 3 directory metadata entries.
- `--datestamp` enables CP/M 3 directory metadata support used for file timestamps.
- Named formats can come from built-ins or from a `--diskdefs` file.
- For `info/list/add/extract/rename/copy/remove/bootread/bootwrite/sysgen`: geometry is auto-detected by image size if no hint is supplied.
- If auto-detection fails, use `-f/--format` or pass full geometry.

## CP/M behavior notes

- Host filenames are converted to uppercase CP/M `8.3` names.
- Name part longer than 8 characters or extension longer than 3 is rejected.
- `list` sizes are reported in CP/M record units (128-byte granularity), so small files may appear rounded up.
- `remove` marks directory entries as deleted (`0xE5`); data blocks are reclaimed by future allocations.
- `sysgen` writes raw bytes into reserved boot/system tracks; correct bootability depends on your platform-specific loader layout.

## cpmtools compatibility

Disk geometry matches cpmtools-style diskdefs. Built-in names are:

- `idpfdd`
- `idphdd`

`cpmdisk` format options accept short names (`fdd`, `hdd`) and full names (`idpfdd`, `idphdd`).

## Project layout

```text
.
├── CMakeLists.txt
├── LICENSE
├── README.md
├── include/
│   └── cpm/
│       ├── cpm_disk.h
│       ├── diskdef.h
│       └── direntry.h
├── lib/
│   ├── CMakeLists.txt
│   ├── cpm_disk.cpp
│   └── print_compat.h
└── src/
    ├── CMakeLists.txt
    ├── main.cpp
    └── print_compat.h
```

## License

This project is licensed under GNU General Public License version 2 only.
See [LICENSE](LICENSE).

Third-party:

- CLI11: BSD-3-Clause

[language.url]: https://isocpp.org/
[language.badge]: https://img.shields.io/badge/language-C++-blue.svg
[standard.url]: https://en.wikipedia.org/wiki/C%2B%2B#Standardization
[standard.badge]: https://img.shields.io/badge/C%2B%2B-23-blue.svg
[status.url]: .
[license.url]: LICENSE
[license.badge]: https://img.shields.io/badge/license-GPL--2.0--only-blue.svg
[status.badge]: https://img.shields.io/badge/status-stable-green.svg
