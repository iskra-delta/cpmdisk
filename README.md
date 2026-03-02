# idpdisk

A command-line tool for creating and managing CP/M disk images for the
**Iskra Delta Partner** — a Z80-based personal computer manufactured in Yugoslavia
in the 1980s.  It supports both the floppy disk (FDD) and hard disk (HDD)
image formats used by the Partner's CP/M 2.2 operating system.

The tool follows the same disk geometry definitions as
[cpmtools](https://www.moria.de/~michael/cpmtools/), so images created here
are compatible with cpmtools and any emulator that understands the raw sector
format (e.g. RunCPM, MAME).

---

## Features

| Command  | Description                                              |
|----------|----------------------------------------------------------|
| `create` | Create a blank FDD or HDD disk image                     |
| `info`   | Show disk geometry, directory usage, and free space      |
| `list`   | List files (optionally filtered by CP/M user area)       |
| `add`    | Copy one or more host files into a user area on the disk |
| `remove` | Delete files by wildcard pattern (`*` and `?` supported) |

---

## Disk types

| Type  | cpmtools name | Tracks | Sec/trk | Sec size | Block size | Max dir | Boot trks | Image size |
|-------|---------------|--------|---------|----------|------------|---------|-----------|------------|
| `fdd` | `idpfdd`      | 146    | 18      | 256 B    | 2 048 B    | 128     | 2         | 657 KB     |
| `hdd` | `idphdd`      | 1 224  | 32      | 256 B    | 4 096 B    | 1 024   | 1         | 9.6 MB     |

---

## Requirements

| Tool / Library | Version  | Notes                                  |
|----------------|----------|----------------------------------------|
| GCC            | ≥ 13     | C++23 required (`-std=gnu++23`)        |
| CMake          | ≥ 3.26   |                                        |
| CLI11          | 2.4.2    | Downloaded automatically via FetchContent |
| git            | any      | Needed by FetchContent to fetch CLI11  |

---

## Building

```bash
# 1. Clone the repository
git clone <repo-url> idp-disk
cd idp-disk

# 2. Configure (Debug build by default)
cmake -B build

# 3. Build
cmake --build build --parallel

# The executable is placed in bin/idpdisk
./bin/idpdisk --help
```

To build a Release binary:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

To clean all build and output artifacts:

```bash
rm -rf build bin
```

### VS Code

Open the repository folder in VS Code.

| Action | Key / menu |
|--------|-----------|
| Build  | `Ctrl+Shift+B` (default build task) |
| Debug  | `F5` — launches `idpdisk create test.dsk fdd` under GDB |

Three debug launch configurations are provided:
- **create fdd** — create a blank FDD image as `test.dsk`
- **list test.dsk** — list its contents
- **info test.dsk** — show geometry and free-space statistics

---

## Usage

```
idpdisk <command> <disk.dsk> [options] [arguments]
```

### create

Create a new, blank disk image.  The file must not already exist.

```bash
idpdisk create myboot.dsk fdd
idpdisk create harddisk.dsk hdd
```

### info

Show geometry and allocation statistics for an existing image.

```bash
idpdisk info myboot.dsk
```

Example output:

```
Disk image : myboot.dsk
Disk type  : idpfdd

Geometry
  Tracks          : 146
  Sectors / track : 18
  Sector size     : 256 bytes
  Boot tracks     : 2
  Block size      : 2048 bytes
  Total blocks    : 324
  Directory blocks: 2
  Disk size       : 672768 (657.0 KB)

Directory
  Capacity        : 128 entries
  Used            : 3
  Free            : 125

Allocation
  Total blocks    : 324
  Used blocks     : 6
  Free blocks     : 318
  Free space      : 651264 (636.0 KB)
```

### list

List all files on the disk.  Use `-u` to restrict to one CP/M user area.

```bash
idpdisk list myboot.dsk          # all user areas
idpdisk list myboot.dsk -u 0     # user area 0 only
```

Example output:

```
User  Name              Size
----  ------------  ----------
   0  COMMAND.COM       20 KB
   0  AUTOEXEC.BAT       1 KB
   3  HELLO.BAS        512 B

3 file(s)
```

> **Note:** CP/M tracks file size to the nearest 128-byte record boundary.
> The size shown may be up to 127 bytes larger than the actual host file size.

### add

Copy one or more host files onto the disk.  Files are placed in the specified
CP/M user area (default: 0).  Shell wildcards are expanded by the shell before
the tool sees the arguments.

```bash
# Add a single file to user area 0
idpdisk add myboot.dsk COMMAND.COM

# Add multiple files
idpdisk add myboot.dsk -u 0 *.com *.bas

# Add to a different user area
idpdisk add myboot.dsk -u 3 myprog.com data.dat
```

Filenames are converted to uppercase CP/M 8.3 format.  Names longer than
8 characters or extensions longer than 3 characters are rejected.

### remove

Delete files whose `NAME.EXT` matches one or more wildcard patterns.
Matching is case-insensitive.  Use `-u` to restrict to one user area.

```bash
# Remove a specific file from any user area
idpdisk remove myboot.dsk OLDFILE.COM

# Remove all .COM files from user area 0
idpdisk remove myboot.dsk -u 0 '*.COM'

# Remove by partial name with ? wildcard
idpdisk remove myboot.dsk 'TEST?.BAS'

# Multiple patterns in one call
idpdisk remove myboot.dsk '*.BAK' '*.TMP'
```

> Removal marks directory entries as deleted (user byte = 0xE5), which is
> standard CP/M behaviour.  Data blocks are not zeroed and will be reclaimed
> automatically when new files are added.

---

## Integration with cpmtools

Images created by `idpdisk` can be used directly with
[cpmtools](https://www.moria.de/~michael/cpmtools/).  Add the following
definitions to your `diskdefs` file (usually `/etc/cpmtools/diskdefs` or
`~/.cpmtools/diskdefs`):

```
diskdef idpfdd
  seclen 256
  tracks 146
  sectrk 18
  blocksize 2048
  maxdir 128
  skew 0
  boottrk 2
  os 3
end

diskdef idphdd
  seclen 256
  tracks 1224
  sectrk 32
  blocksize 4096
  maxdir 1024
  skew 0
  boottrk 1
  os 3
end
```

Then you can use standard cpmtools commands:

```bash
cpmls  -f idpfdd myboot.dsk
cpmcp  -f idpfdd myboot.dsk 0:COMMAND.COM ./COMMAND.COM
cpmdump -f idpfdd myboot.dsk | less
```

---

## Project structure

```
idp-disk/
├── README.md
├── .gitignore
├── CMakeLists.txt            Project root: C++23 standard, bin/ output, CLI11, add_subdirectory(src)
├── .vscode/
│   ├── tasks.json            Build and clean tasks
│   └── launch.json           Debug launch configurations
└── src/
    ├── CMakeLists.txt        Target definition only (add_executable + compile options)
    ├── diskdef.h             Disk geometry constants and derived calculations
    ├── direntry.h            CP/M 2.2 directory entry layout and helpers
    ├── cpm_disk.h            CpmDisk class interface
    ├── cpm_disk.cpp          Disk creation, I/O, and all commands
    ├── print_compat.h        println() shim for GCC < 14
    └── main.cpp              CLI entry point (CLI11)
```

Build output:

```
build/    CMake intermediate files (safe to delete)
bin/      Compiled executable: idpdisk
```

---

## License

Copyright (C) 2024 Tomaž Štih and contributors.

This program is free software; you can redistribute it and/or modify it under
the terms of the **GNU General Public License version 2** as published by the
Free Software Foundation.

This program is distributed in the hope that it will be useful, but **WITHOUT
ANY WARRANTY**; without even the implied warranty of **MERCHANTABILITY** or
**FITNESS FOR A PARTICULAR PURPOSE**.  See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0.html>.

### Third-party components

| Component | License    | URL                                        |
|-----------|------------|--------------------------------------------|
| CLI11     | BSD 3-Clause | https://github.com/CLIUtils/CLI11        |
