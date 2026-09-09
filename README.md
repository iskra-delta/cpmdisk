[![status.badge]][status.url] [![language.badge]][language.url] [![standard.badge]][standard.url] [![license.badge]][license.url]

# cpmdisk

`cpmdisk` is a command-line tool for creating and managing CP/M disk images used by the Iskra Delta Partner.

It supports the Partner's known floppy and hard disk geometries and can also work with fully custom geometries when all required parameters are supplied.

## Architecture

This project is intentionally split into two parts:

- Frontend: `cpmdisk` CLI in `src/main.cpp` (argument parsing and UX).
- Backend: shared CP/M library target `cpmdisk_lib` (artifact: `libcpmdisk.*` / `cpmdisk.dll`) in `lib/`, with public headers in `include/cpmdisk/`.

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
- `fix`: repack directory entries for the geometry's extent mask (EXM)
- `bootread`: export reserved boot/system tracks
- `bootwrite`: import reserved boot/system tracks
- `sysgen`: place CP/M system image into boot/system tracks

## Disk formats

Built-in named formats:

| Type | Internal name | Tracks | Sec/trk | Sector size | Block size | Max dir | Boot trk | Size | Machine |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| `fdd`, `fdd:g` | `idpfdd` | 146 | 18 | 256 | 2048 | 128 | 2 | 672,768 B | Partner G (GDP) |
| `fdd:p` | `idpfdd:p` | 154 | 18 | 256 | 2048 | 128 | 2 | 709,632 B | Partner P (CRT) |
| `hdd`, `hdd:g`, `hdd:p` | `idphdd` | 1224 | 32 | 256 | 4096 | 1024 | 1 | 10,027,008 B | both |

### Selecting a Partner model with `:g` / `:p`

The Partner shipped two floppy media that share **every** CP/M parameter except
the track count -- CP/M describes each physical head as a logical track, so the
count is twice the cylinder count:

- Partner G (GDP): 73 cylinders x 2 heads = 146 tracks
- Partner P (CRT): 77 cylinders x 2 heads = 154 tracks

Append `:g` or `:p` to a type name to pick one:

```bash
cpmdisk create boot.dsk fdd        # Partner G, 146 tracks (default, unchanged)
cpmdisk create boot.dsk fdd:g      # the same disk, stated explicitly
cpmdisk create boot.dsk fdd:p      # Partner P, 154 tracks
```

A bare `fdd` always means the Partner G medium, so adding media never changes an
existing command line.  The suffix is case-insensitive.  Both models use the same
10 MB hard disk, so `hdd`, `hdd:g` and `hdd:p` are one and the same drive.

Size auto-detection recognises both floppies, so `-f/--format` is only needed for
custom geometry:

```bash
cpmdisk info fdd-partner-p.img     # -> Disk type : idpfdd:p
```

Because only `DSM` differs (324 blocks against 342), the two floppy formats are
interchangeable in practice until a disk fills past 648 KB, which is why an image
built for one model boots happily on the other.  Beyond that point a G-format
disk described as P lets CP/M allocate blocks that are not in the image.

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

- `include/cpmdisk/cpmdisk.h` (umbrella include)
- `include/cpmdisk/cpm_disk.h`
- `include/cpmdisk/diskdef.h`
- `include/cpmdisk/direntry.h`

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

- `-f, --format <name>`: force a named geometry (`fdd`, `fdd:p`, `hdd`, or a name from `--diskdefs`)
- `--diskdefs <file>`: load additional named formats from a cpmtools `diskdefs` file
- `--seclen`, `--tracks`, `--sectrk`, `--blocksize`, `--maxdir`, `--skew`, `--boottrk`:
  explicit geometry override fields
- `--exm <0|1|3|7|15>`: extent mask override (see [Extents and the extent mask](#extents-and-the-extent-mask));
  omit it and the mask is derived from block size and pointer width

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

### fix

Repack every file's directory entries into the canonical layout for the disk's
extent mask.  Safe to re-run: a directory that is already correct is reported and
left untouched.

```bash
cpmdisk fix hdd.dsk --dry-run
cpmdisk fix hdd.dsk
```

Use it on images written by cpmdisk **before** extent-mask support, on any format
whose EXM is greater than 0 (`hdd`, for example).  Such images list and extract
correctly under `cpmdisk`, but real CP/M reads their multi-extent files short --
see [Extents and the extent mask](#extents-and-the-extent-mask).

`fix` reports, per file, how many directory entries it collapsed:

```text
fix       0:BIG.BIN - 3 entries -> 2
fix       0:HUGE.BIN - 7 entries -> 4

Repacked 2 file(s) for EXM 1, freed 4 directory entries.
```

Files are left untouched, with a `skip` line, when the directory cannot be
repacked safely: duplicate logical extent numbers, or an allocation that does not
match the record count implied by `EX`/`RC` (a sparse, randomly written file).

`fix` normalises rather than merely repairs, so it also drops the empty trailing
extent (`RC 0`, no blocks) that CP/M leaves behind when a file ends on an exact
extent boundary.  That is semantically neutral -- reading stops at the same byte
either way -- but it does mean `fix` can report a change on a directory written
by real CP/M.  Use `--dry-run` first if you want to see what it would touch.

Because `fix` rebuilds from the geometry, `--exm` also lets it convert in the
other direction, which is useful when a machine's BIOS does not follow the
standard derivation:

```bash
cpmdisk fix hdd.dsk --exm 0     # one entry per 16 KB logical extent
cpmdisk fix hdd.dsk             # back to the derived EXM 1
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
- `--exm` (extent mask; derived when omitted)
- `--diskdefs` (for named format lookup from a cpmtools `diskdefs` file)

Rules:

- For `create`: provide either a named type (`fdd`/`hdd`) or all required geometry fields.
- Partner types (`fdd`, `hdd`, `idpfdd`, `idphdd`) default to CP/M 3 mode.
- Custom geometry defaults to CP/M 2.2 mode; pass `--cpm3` to enable CP/M 3 mode.
- `--label` and `--datestamp` require CP/M 3 mode and add CP/M 3 directory metadata entries.
- `--datestamp` enables CP/M 3 directory metadata support used for file timestamps.
- Named formats can come from built-ins or from a `--diskdefs` file.
- For `info/list/add/extract/rename/copy/remove/fix/bootread/bootwrite/sysgen`: geometry is auto-detected by image size if no hint is supplied.
- A partial set of overrides (say `--exm 1` alone) is applied on top of the auto-detected type.
- If auto-detection fails, use `-f/--format` or pass full geometry.

## CP/M behavior notes

- Host filenames are converted to uppercase CP/M `8.3` names.
- Name part longer than 8 characters or extension longer than 3 is rejected.
- `list` sizes are reported in CP/M record units (128-byte granularity), so small files may appear rounded up.
- `remove` marks directory entries as deleted (`0xE5`); data blocks are reclaimed by future allocations.
- `sysgen` writes raw bytes into reserved boot/system tracks; correct bootability depends on your platform-specific loader layout.

## Extents and the extent mask

CP/M splits a file into *logical extents* of 128 records x 128 bytes = 16 KB.  A
*physical* directory entry holds a fixed number of block pointers -- 16 one-byte
pointers, or 8 two-byte pointers once the disk has more than 256 allocation
blocks.  When those pointers address more than 16 KB, one directory entry carries
several logical extents at once, and the DPB's extent mask `EXM` says how many:

| Block size | Pointers per entry | Bytes per entry | Extents per entry | EXM |
|---:|---:|---:|---:|---:|
| 1024 | 16 x 1 byte | 16 KB | 1 | 0 |
| 2048 | 16 x 1 byte | 32 KB | 2 | 1 |
| 2048 | 8 x 2 bytes | 16 KB | 1 | 0 |
| 4096 | 16 x 1 byte | 64 KB | 4 | 3 |
| 4096 | 8 x 2 bytes | 32 KB | 2 | 1 |
| 8192 | 8 x 2 bytes | 64 KB | 4 | 3 |

In an entry that carries several logical extents, `XL`/`XH` hold the number of the
**last** logical extent present and `RC` counts the records of that last extent;
every earlier extent in the same entry is implicitly full.  The BDOS masks the FCB
extent number with `EXM` before comparing it against the directory, so two entries
whose extent numbers differ only in the masked bits are, to CP/M, the *same*
physical extent: only the first is ever found, and everything beyond it becomes
unreachable.  Reading such a file stops at the first entry's worth of data.

Both built-in Partner formats sit on opposite sides of this:

- `fdd` -- 2048-byte blocks, word pointers, 16 KB per entry: **EXM 0**, one entry
  per logical extent.
- `hdd` -- 4096-byte blocks, word pointers, 32 KB per entry: **EXM 1**, one entry
  per *pair* of logical extents.

`cpmdisk` derives `EXM` from the geometry, packs entries accordingly on `add` and
`copy`, and can repack an existing directory with [`fix`](#fix).  `info` reports
the derived value:

```text
Extents
  Block pointers  : 16-bit, 8 per entry
  Extent mask EXM : 1 (derived)
  Extents / entry : 2 x 16 KB
  Blocks / entry  : 8
```

Pass `--exm` to override the derivation for a BIOS that does not follow it.  The
value must be one of `0`, `1`, `3`, `7`, `15`, and it changes how entries are both
written and repacked, so it must match the machine's actual DPB.

> Images written by `cpmdisk` before this was implemented store one directory
> entry per logical extent regardless of `EXM`.  On `hdd` that makes every file
> larger than 16 KB read short under real CP/M, while still listing and extracting
> correctly under `cpmdisk` itself.  Run `cpmdisk fix` on such images.

## cpmtools compatibility

Disk geometry matches cpmtools-style diskdefs. Built-in names are:

- `idpfdd` (Partner G floppy; `idpfdd:p` for the Partner P floppy)
- `idphdd`

`cpmdisk` format options accept short names (`fdd`, `hdd`) and full names (`idpfdd`, `idphdd`).

`diskdefs` files are read with the standard cpmtools keys (`seclen`, `tracks`,
`sectrk`, `blocksize`, `maxdir`, `skew`, `boottrk`, `offset`).  One `cpmdisk`
extension is recognised in addition:

```text
diskdef mymachine
  seclen 512
  tracks 64
  sectrk 16
  blocksize 4096
  maxdir 64
  boottrk 2
  exm 1        # cpmdisk extension: extent mask, derived when omitted
end
```

`exm` is unknown to cpmtools; leave it out of files you share with it.

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
