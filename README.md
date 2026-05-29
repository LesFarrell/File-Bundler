# File Bundler

`file_bundler.exe` is a native Windows packer/launcher written in C.
It operates in two modes:

- Builder mode: shows a GUI that copies the current EXE, appends bundled file data, writes a manifest, and finishes with a footer.
- Bundled mode: detects the footer on startup, extracts the appended files beside the EXE, optionally launches a configured inner EXE, and may clean the extracted files back up afterward.

## Build

Run:

```bat
build_File_Bundler.bat
```

The build script:

1. Generates `application.rc` if it does not exist.
2. Compiles the icon resource into `application.res`.
3. Builds a 64-bit `file_bundler.exe` with Zig (`x86_64-windows-gnu`) and links the Windows GUI libraries it needs, including `Cabinet` for the Windows Compression API.
4. Runs a post-build strip step with `strip.exe` or `llvm-strip.exe` when one is available on `PATH`.

## User Flow

1. Start `file_bundler.exe`.
2. Choose a source folder.
3. Optionally choose a startup EXE inside that folder.
4. Choose an output folder and bundle name.
5. Optionally choose whether the bundle should extract into a temp folder and launch from there.
6. Choose the compression mode the builder should use: `Store only`, `XPRESS`, or `XPRESS_HUFF`.
7. Optionally choose a custom `.ico` file or an `.exe` whose original icon should be copied to the generated bundle.
8. Click `Build Bundle`.

## Developer Overview

The code in [file_bundler.c](/abs/c:/development/testing/playing/file_bundler/file_bundler.c) is organized around a few main responsibilities:

- Data model and helpers: path conversion, file list management, manifest parsing, compression, and file I/O helpers.
- Builder path: `build_bundle()` gathers files, copies the stub EXE, optionally rewrites icon resources, appends file payloads, writes the manifest, and appends the footer.
- Launcher path: `run_bundled_mode()` checks for a valid footer. If one exists, it extracts files, launches the configured startup EXE if present, and performs cleanup depending on the saved options.
- GUI path: `create_child_controls()`, `main_wnd_proc()`, and the small settings helpers manage the Win32 interface and persist the last-used values in an `.ini` file beside the EXE.

## Key Structures

These are the core record types used by the format and runtime:

- `FileEntry`: one source file during bundle creation. Stores the original path, UTF-8 relative path, file sizes, data offset, and compression type.
- `ManifestEntry`: one file record loaded back out of the manifest during extraction.
- `Buffer`: generic byte buffer used for file loads and compression output.
- `BundleFooter`: the fixed-size trailer at the end of the EXE. It identifies the file as a bundle and tells the loader where the manifest starts.

The icon-related structs are just in-memory views of `.ico` and grouped icon resource headers used by the icon-copy helpers.

## Runtime Behavior

### Builder mode

`wWinMain()` first calls `run_bundled_mode()`. If no valid footer is found, execution falls through into the GUI.

When `build_bundle()` runs, it:

1. Reads the UI values and validates them.
2. Recursively enumerates every file under the selected source folder.
3. Copies the current `file_bundler.exe` to the output location. This copied EXE becomes the stub/launcher.
4. Optionally rewrites the icon resources in the copied EXE from an `.ico` file or from another executable's grouped icon resources. If you leave the icon source blank and choose a startup EXE, the bundle now tries to inherit that startup EXE's original icon automatically.
5. Packs each bundled file with the selected compression mode. `Store only` writes files raw, while `XPRESS` and `XPRESS_HUFF` only keep the compressed payload when it is smaller than the original.
6. Writes the manifest after the payload region, including runtime option flags such as `keep_files` and `extract_to_temp`.
7. Appends a `BundleFooter` so future launches can locate the manifest quickly.

The output is a copy of the builder executable with payload bytes, manifest data, and the footer appended to the end.

Compression is per-file. The current implementation supports:

- `0`: no compression
- `1`: Windows `XPRESS_HUFF`
- `2`: Windows `XPRESS`

`pack_file_data()` uses the compression mode selected in the builder. For `XPRESS` and `XPRESS_HUFF`, it only keeps the compressed form if it is smaller than the original. Files that do not benefit are stored raw.

### Bundled mode

If `run_bundled_mode()` finds a valid footer:

1. It opens the EXE and reads the manifest from the recorded offset.
2. It extracts bundled files either into the same folder as the EXE or into a unique temp folder, depending on the saved runtime options.
3. If no startup EXE was recorded, it stops after extraction and shows the destination folder.
4. If a startup EXE was recorded, it launches it with the extraction folder as the working directory.
5. If `keep_files` is false, it waits for the child process to exit and then deletes the extracted files.
6. If `keep_files` is true, it leaves the extracted files in place and exits without cleanup.

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

The last bytes of the file are always:

```c
typedef struct {
    char magic[8];          // "BUNDLE01"
    uint32_t version;       // currently 1
    uint64_t manifest_offset;
} BundleFooter;
```

The loader seeks to `sizeof(BundleFooter)` bytes from EOF, verifies the `BUNDLE01` magic/version, and then jumps to `manifest_offset`.

### Manifest layout

The manifest begins at `BundleFooter.manifest_offset` and is written in this order:

1. `uint32_t version`
2. `uint32_t file_count`
3. `uint32_t startup_len`
4. `uint32_t runtime_options`
5. `startup_len` bytes of UTF-8 startup path, if present
6. `file_count` file records

Current runtime option bits:

- `0x1`: keep extracted files after the child process exits
- `0x2`: extract into a unique temp folder and launch from there

Each file record is:

1. `uint32_t path_len`
2. `uint64_t data_offset`
3. `uint64_t file_size`
4. `uint64_t stored_size`
5. `uint32_t compression_type`
6. `path_len` bytes of UTF-8 relative path

Notes:

- `data_offset` points to the file payload earlier in the EXE, not to data inside the manifest.
- `file_size` is the original uncompressed size.
- `stored_size` is the byte count actually written to the EXE.
- For `compression_type == 0`, `stored_size` must match `file_size`.
- Relative paths are stored in UTF-8 and reconstructed to wide strings during extraction.

### XPRESS_HUFF format

Compressed files use `compression_type == 1`, which means the payload was compressed with the Windows Compression API in buffer mode using `COMPRESS_ALGORITHM_XPRESS_HUFF`.

Compressed files with `compression_type == 2` use `COMPRESS_ALGORITHM_XPRESS`.

## Extraction and Cleanup Notes

- Extraction can target either the directory containing the bundle EXE or a unique temp directory, depending on the saved runtime options.
- The startup EXE path is stored relative to the source folder so the same layout works after extraction.
- Cleanup is path-driven: the loader deletes each extracted file listed in the manifest and then removes empty parent directories.
- Cleanup now also removes any directories that were created during extraction, recursively deleting extra files and subfolders the launched app wrote inside those extraction-created directories.
- If no startup EXE is configured, extracted files are left in place because there is no child process to wait on and no cleanup pass is triggered.

## Persisted State

Builder UI state is saved in an `.ini` file beside the executable. The `builder` section stores:

- `source`
- `startup`
- `output_folder`
- `bundle_name`
- `icon`
- `keep_files`
- `extract_to_temp`
- `compression_mode`

## Things To Watch When Modifying It

- The footer and manifest layout are the contract between builder mode and bundled mode. If either changes, both writer and reader must be updated together.
- Extraction treats manifest metadata as untrusted. Size conversions and decompression bounds checks should stay strict.
- `#pragma pack(push, 1)` on the footer/resource structs matters. Removing it changes the on-disk binary layout.
- `run_bundled_mode()` is intentionally called before the GUI is created. That is what makes the same EXE act as both builder and launcher.
- The program enters through `wWinMain()` and funnels startup into the shared `app_main()` path.
- `apply_icon_to_exe()` edits PE resources in-place on the copied EXE. It does not modify the original running executable.
- Path handling is wide-char on Windows, but manifest paths are stored as UTF-8. Changes around conversion need to preserve that boundary carefully.
