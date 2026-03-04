# cpmdisk

`cpmdisk` is a command-line tool for creating and managing CP/M disk images used by the Iskra Delta Partner.

It supports the Partner's known floppy and hard disk geometries and can also work with fully custom geometries when all required parameters are supplied.

## Current scope

Implemented commands:

- `create`: create a blank disk image
- `info`: show geometry and free-space statistics
- `list`: list files (optionally by CP/M user area)
- `add`: copy host files into a CP/M user area
- `remove`: delete by wildcard pattern (`*`, `?`)

Not implemented in this repository:

- file extraction back to host
- file rename/copy within image
- boot sector/system track tooling

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

Release build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

## CLI overview

```text
cpmdisk <command> <disk> [options]
```

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

### remove

Delete files by wildcard pattern (`*`, `?`), optionally restricted to one user area:

```bash
cpmdisk remove boot.dsk '*.BAK'
cpmdisk remove boot.dsk -u 0 '*.COM'
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
- For `info/list/add/remove`: geometry is auto-detected by image size if no hint is supplied.
- If auto-detection fails, use `-f/--format` or pass full geometry.

## CP/M behavior notes

- Host filenames are converted to uppercase CP/M `8.3` names.
- Name part longer than 8 characters or extension longer than 3 is rejected.
- `list` sizes are reported in CP/M record units (128-byte granularity), so small files may appear rounded up.
- `remove` marks directory entries as deleted (`0xE5`); data blocks are reclaimed by future allocations.

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
└── src/
    ├── CMakeLists.txt
    ├── main.cpp
    ├── cpm_disk.h
    ├── cpm_disk.cpp
    ├── diskdef.h
    ├── direntry.h
    └── print_compat.h
```

## License

This project is licensed under GNU General Public License version 2 only.
See [LICENSE](LICENSE).

Third-party:

- CLI11: BSD-3-Clause
