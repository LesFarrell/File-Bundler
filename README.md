# File Bundler

`FileBundler.exe` is a native Win32 packer/launcher written in C.

Copyright (c) 2026 Les Farrell.

The same executable runs in two modes:

- Builder mode: shows a GUI, copies itself, appends bundled file data, writes a manifest, and finishes with a footer.
- Bundled mode: detects that footer on startup, shows extraction progress, extracts the embedded files, optionally launches a bundled EXE, and may clean the extracted files up afterward.

## What It Does

- Bundles an entire folder into a single Windows `.exe`
- Optionally launches one bundled `.exe` after extraction
- Supports `Store only`, `XPRESS`, and `XPRESS_HUFF` compression
- Can extract either beside the bundle or into a temporary folder
- Can keep extracted files or delete them after the launched process exits
- Can apply a custom icon from an `.ico`
- If no custom `.ico` is selected, inherits the startup EXE icon when one is configured
- Shows a progress bar and auto-scrolling status log while building
- Runs bundle creation on a background thread so the builder window stays responsive
- Shows a small progress dialog while a bundle is extracting at startup
- Persists the builder UI state in an `.ini` file beside the executable

## Project Files

- [filebundler.c](filebundler.c): application code, bundle writer/reader, extraction logic, and Win32 GUI
- [premake5.lua](premake5.lua): Premake project generator for Visual Studio or MinGW-style builds
- [application.ico](application.ico): default application icon
- [application.rc](application.rc): Windows resource script

## Requirements

- Windows
- Premake5 plus a supported compiler toolchain, for generated projects

The program depends on standard Win32 libraries plus:

- `cabinet` for the Windows Compression API
- `comctl32` for progress bar controls
- `gdi32` for dialog font setup

## Build

### Premake5

Generate Visual Studio 2022 files:

```bat
premake5 vs2022
```

Generate GNU Make files:

```bat
premake5 gmake
```

Build with Zig:

```bat
build_zig.bat
```

This builds the executable directly with `zig cc` as a Windows GUI application.

The Premake configuration:

- builds a 64-bit Windows GUI executable
- compiles `filebundler.c` together with the resource file
- keeps `wWinMain()` as the Unicode entry point
- links `cabinet`, `shell32`, `comctl32`, `comdlg32`, `gdi32`, `ole32`, and `user32`
- tunes Release builds for smaller output

Builds output `FileBundler.exe` in the repo root.

## How To Use

1. Start `FileBundler.exe`.
2. Choose a source folder.
3. Optionally choose a startup EXE relative to that source folder.
4. Choose an output folder and bundle name.
5. Optionally choose whether extraction should happen in a temp folder.
6. Choose a compression mode.
7. Optionally choose a custom icon source.
8. Click `Build Bundle`.
9. Watch the progress bar and status log while files are packed.

During a build, the builder disables its input controls, keeps the window responsive, and auto-scrolls the status log to the newest line.

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
- The icon source, if set, must be an existing `.ico`.

If no icon source is selected and a startup EXE is configured, the builder uses the startup EXE's icon automatically.

## Runtime Behavior

`wWinMain()` first calls `run_bundled_mode()`. If no valid bundle footer is present, the program falls through into the builder GUI.

When running as a bundle:

1. The loader reads the footer at EOF.
2. It reads the manifest and runtime option flags.
3. It opens a small extraction dialog with the current file path and a progress bar.
4. It extracts files either beside the bundle or into a unique temp folder.
5. If no startup EXE was stored, it closes the progress dialog, stops after extraction, and shows the destination folder.
6. If a startup EXE was stored, it closes the progress dialog, launches it with the extraction folder as the working directory, and forwards any command-line arguments that were passed to the bundle.
7. If `keep_files` is off, cleanup runs after the child process exits.
8. If `keep_files` is on, the extracted files are left in place.

Cleanup behavior depends on extraction mode:

- Temp extraction: the temp directory is deleted recursively.
- Extraction beside the bundle: files listed in the manifest are deleted, empty parent directories are removed, and extraction-created directories are cleaned up.

## Compression

Builder UI modes:

- `Store only`
- `XPRESS`
- `XPRESS_HUFF`

Compression is applied per file. For `XPRESS` and `XPRESS_HUFF`, the builder only keeps the compressed payload when it is smaller than the original file. Otherwise that file is stored raw.

Progress reporting also follows the per-file model:

- Builder progress advances as each file finishes packing.
- Runtime extraction progress advances as each file finishes extracting.
- A single very large file shows its name immediately, but the bar only moves again when that file completes.

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

## Copyright

Copyright (c) 2026 Les Farrell.
