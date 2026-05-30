# File Bundler

`file_bundler.exe` is a native Win32 packer/launcher written in C.

The same executable runs in two modes:

- Builder mode: shows a GUI, copies itself, appends bundled file data, writes a manifest, and finishes with a footer.
- Bundled mode: detects that footer on startup, extracts the embedded files, optionally launches a bundled EXE, and may clean the extracted files up afterward.

## What It Does

- Bundles an entire folder into a single Windows `.exe`
- Optionally launches one bundled `.exe` after extraction
- Supports `Store only`, `XPRESS`, and `XPRESS_HUFF` compression
- Can extract either beside the bundle or into a temporary folder
- Can keep extracted files or delete them after the launched process exits
- Can apply a custom icon from an `.ico` or copy an icon from another `.exe`
- Persists the builder UI state in an `.ini` file beside the executable

## Project Files

- [file_bundler.c](file_bundler.c): application code, bundle writer/reader, extraction logic, and Win32 GUI
- [build_File_Bundler.bat](build_File_Bundler.bat): Zig-based one-step build
- [premake5.lua](premake5.lua): Premake project generator for Visual Studio or MinGW-style builds
- [application.ico](application.ico): default application icon
- [application.rc](application.rc): Windows resource script

## Requirements

- Windows
- One of:
  - Zig, for the batch build
  - Premake5 plus a supported compiler toolchain, for generated projects

The program depends on standard Win32 libraries plus `cabinet` for the Windows Compression API.

## Build

### Zig batch build

Run:

```bat
build_File_Bundler.bat
```

The script:

1. Creates `application.rc` if it does not already exist.
2. Compiles the icon resource into `application.res`.
3. Builds a 64-bit GUI executable with Zig targeting `x86_64-windows-gnu`.
4. Runs `strip` or `llvm-strip` if one is available on `PATH`.

### Premake5

Generate Visual Studio 2022 files:

```bat
premake5 vs2022
```

Generate GNU Make files:

```bat
premake5 gmake
```

The Premake configuration:

- builds a 64-bit Windows GUI executable
- compiles `file_bundler.c` together with the resource file
- keeps `wWinMain()` as the Unicode entry point
- links `cabinet`, `shell32`, `comdlg32`, `ole32`, and `user32`
- tunes Release builds for smaller output

Release builds output `file_bundler.exe` in the repo root. Debug builds output `file_bundler_debug.exe`.

## How To Use

1. Start `file_bundler.exe`.
2. Choose a source folder.
3. Optionally choose a startup EXE relative to that source folder.
4. Choose an output folder and bundle name.
5. Optionally choose whether extraction should happen in a temp folder.
6. Choose a compression mode.
7. Optionally choose a custom icon source.
8. Click `Build Bundle`.

## Builder Rules

- The source folder must exist and contain at least one file.
- The output folder must exist.
- The bundle name must be a valid Windows filename.
- The output bundle must be outside the source folder.
- The startup EXE, if set, must:
  - be a relative path
  - point to an `.exe`
  - stay inside the source folder
- The bundle name cannot match:
  - the configured startup EXE name
  - a bundled root-level EXE name that would collide during extraction
- The icon source, if set, must be an existing `.ico` or `.exe`.

If no icon source is selected and a startup EXE is configured, the builder tries to inherit the startup EXE's icon automatically.

## Runtime Behavior

`wWinMain()` first calls `run_bundled_mode()`. If no valid bundle footer is present, the program falls through into the builder GUI.

When running as a bundle:

1. The loader reads the footer at EOF.
2. It reads the manifest and runtime option flags.
3. It extracts files either beside the bundle or into a unique temp folder.
4. If no startup EXE was stored, it stops after extraction and shows the destination folder.
5. If a startup EXE was stored, it launches it with the extraction folder as the working directory.
6. If `keep_files` is off, cleanup runs after the child process exits.
7. If `keep_files` is on, the extracted files are left in place.

Cleanup behavior depends on extraction mode:

- Temp extraction: the temp directory is deleted recursively.
- Extraction beside the bundle: files listed in the manifest are deleted, empty parent directories are removed, and extraction-created directories are cleaned up.

## Compression

Builder UI modes:

- `Store only`
- `XPRESS`
- `XPRESS_HUFF`

Compression is applied per file. For `XPRESS` and `XPRESS_HUFF`, the builder only keeps the compressed payload when it is smaller than the original file. Otherwise that file is stored raw.

On-disk bundle compression codes are:

- `0`: uncompressed
- `1`: `XPRESS_HUFF`
- `2`: `XPRESS`

## Bundle Format

The bundle is appended directly to the end of the launcher EXE:

```text
[normal PE executable bytes]
[file payload 0]
[file payload 1]
...
[manifest]
[BundleFooter]
```

### Footer

The file always ends with:

```c
typedef struct {
    char magic[8];          // "BUNDLE01"
    uint32_t version;       // currently 1
    uint64_t manifest_offset;
} BundleFooter;
```

The loader seeks backward from EOF by `sizeof(BundleFooter)`, verifies the magic and version, and then jumps to `manifest_offset`.

### Manifest Layout

The manifest is written in this order:

1. `uint32_t version`
2. `uint32_t file_count`
3. `uint32_t startup_len`
4. `uint32_t runtime_options`
5. `startup_len` bytes of UTF-8 startup path, if present
6. `file_count` file records

Runtime option bits:

- `0x1`: keep extracted files after the child process exits
- `0x2`: extract into a unique temp folder

Each file record is:

1. `uint32_t path_len`
2. `uint64_t data_offset`
3. `uint64_t file_size`
4. `uint64_t stored_size`
5. `uint32_t compression_type`
6. `path_len` bytes of UTF-8 relative path

Notes:

- `data_offset` points into the payload region earlier in the EXE.
- `file_size` is the original size.
- `stored_size` is the byte count actually stored.
- `compression_type == 0` means `stored_size == file_size`.
- Relative paths are stored as UTF-8 and converted back to wide strings during extraction.

## Persisted State

The builder stores UI state in an `.ini` file beside the executable under the `[builder]` section.

Keys:

- `source`
- `startup`
- `output_folder`
- `bundle_name`
- `icon`
- `keep_files`
- `extract_to_temp`
- `compression_mode`

## Development Notes

- The footer and manifest layout are the contract between builder mode and bundled mode. Change both sides together.
- `#pragma pack(push, 1)` on the footer and icon/resource structs matters because those layouts are written to disk or mapped onto binary resource data.
- `apply_icon_to_exe()` only edits the copied output EXE, not the running builder executable.
- Path handling is wide-char in the app, but manifest paths are stored as UTF-8.
- Extraction code treats manifest metadata as untrusted input. Keep size checks and path-boundary checks strict.
