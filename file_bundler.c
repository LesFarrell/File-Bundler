#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>

#ifdef __TINYC__
DECLARE_HANDLE(COMPRESSOR_HANDLE);
typedef COMPRESSOR_HANDLE *PCOMPRESSOR_HANDLE;
typedef COMPRESSOR_HANDLE DECOMPRESSOR_HANDLE;
typedef COMPRESSOR_HANDLE *PDECOMPRESSOR_HANDLE;

#define COMPRESS_ALGORITHM_XPRESS 3
#define COMPRESS_ALGORITHM_XPRESS_HUFF 4
#ifdef swprintf
#undef swprintf
#endif
#define swprintf _snwprintf

BOOL WINAPI CreateCompressor(DWORD algorithm, PVOID allocation_routines, PCOMPRESSOR_HANDLE compressor_handle);
BOOL WINAPI Compress(COMPRESSOR_HANDLE compressor_handle, LPCVOID uncompressed_data, SIZE_T uncompressed_data_size, PVOID compressed_buffer, SIZE_T compressed_buffer_size, PSIZE_T compressed_data_size);
BOOL WINAPI CloseCompressor(COMPRESSOR_HANDLE compressor_handle);
BOOL WINAPI CreateDecompressor(DWORD algorithm, PVOID allocation_routines, PDECOMPRESSOR_HANDLE decompressor_handle);
BOOL WINAPI Decompress(DECOMPRESSOR_HANDLE decompressor_handle, LPCVOID compressed_data, SIZE_T compressed_data_size, PVOID uncompressed_buffer, SIZE_T uncompressed_buffer_size, PSIZE_T uncompressed_data_size);
BOOL WINAPI CloseDecompressor(DECOMPRESSOR_HANDLE decompressor_handle);
#else
#include <compressapi.h>
#endif

#define APP_TITLE L"File Bundler"
#define BUNDLE_MAGIC "BUNDLE01"
#define BUNDLE_VERSION 1u
#define BUNDLE_COMPRESSION_NONE 0u
#define BUNDLE_COMPRESSION_XPRESS_HUFF 1u
#define BUNDLE_COMPRESSION_XPRESS 2u
#define BUNDLE_OPTION_KEEP_FILES 0x1u
#define BUNDLE_OPTION_EXTRACT_TO_TEMP 0x2u
#define BUILDER_COMPRESSION_STORE 0u
#define BUILDER_COMPRESSION_XPRESS 1u
#define BUILDER_COMPRESSION_XPRESS_HUFF 2u
#define PATH_BUFFER_CHARS (MAX_PATH * 4)
#define STATUS_BUFFER_CHARS 2048

#define IDC_SOURCE_EDIT 1001
#define IDC_SOURCE_BROWSE 1002
#define IDC_STARTUP_EDIT 1003
#define IDC_STARTUP_BROWSE 1004
#define IDC_OUTPUT_EDIT 1005
#define IDC_OUTPUT_BROWSE 1006
#define IDC_OUTPUT_NAME_EDIT 1007
#define IDC_ICON_EDIT 1008
#define IDC_ICON_BROWSE 1009
#define IDC_KEEP_FILES_CHECK 1010
#define IDC_EXTRACT_TO_TEMP_CHECK 1011
#define IDC_COMPRESSION_STORE_RADIO 1012
#define IDC_COMPRESSION_XPRESS_RADIO 1013
#define IDC_COMPRESSION_XPRESS_HUFF_RADIO 1014
#define IDC_BUILD_BUTTON 1015
#define IDC_STATUS_EDIT 1016

typedef struct {
    wchar_t *full_path;
    char *relative_utf8;
    uint64_t size;
    uint64_t data_offset;
    uint64_t stored_size;
    uint32_t compression_type;
} FileEntry;

typedef struct {
    FileEntry *items;
    size_t count;
    size_t capacity;
} FileList;

typedef struct {
    wchar_t *relative_path;
    uint64_t data_offset;
    uint64_t file_size;
    uint64_t stored_size;
    uint32_t compression_type;
} ManifestEntry;

typedef struct {
    unsigned char *data;
    size_t size;
} Buffer;

typedef struct {
    wchar_t **items;
    size_t count;
    size_t capacity;
} PathList;

/* The footer is stored verbatim at EOF, so its packed layout is part of the
   on-disk bundle format contract. */
#pragma pack(push, 1)
typedef struct {
    char magic[8];
    uint32_t version;
    uint64_t manifest_offset;
} BundleFooter;

typedef struct {
    WORD idReserved;
    WORD idType;
    WORD idCount;
} IconDirHeader;

typedef struct {
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    DWORD dwImageOffset;
} IconDirEntry;

typedef struct {
    WORD idReserved;
    WORD idType;
    WORD idCount;
} GrpIconDirHeader;

typedef struct {
    BYTE bWidth;
    BYTE bHeight;
    BYTE bColorCount;
    BYTE bReserved;
    WORD wPlanes;
    WORD wBitCount;
    DWORD dwBytesInRes;
    WORD nID;
} GrpIconDirEntry;
#pragma pack(pop)

typedef struct {
    HWND source_edit;
    HWND startup_edit;
    HWND output_edit;
    HWND output_name_edit;
    HWND icon_edit;
    HWND keep_files_check;
    HWND extract_to_temp_check;
    HWND compression_store_radio;
    HWND compression_xpress_radio;
    HWND compression_xpress_huff_radio;
    HWND status_edit;
    HWND build_button;
} UiState;

typedef struct {
    BOOL found;
    BOOL is_integer;
    WORD id;
    wchar_t name[256];
} ResourceNameSelection;

static UiState g_ui = {0};

static void set_default_bundle_name_from_path(HWND edit, const wchar_t *path);
static BOOL bundle_name_is_valid(const wchar_t *name);
static BOOL bundle_name_conflicts_with_startup_exe(const wchar_t *bundle_name, const wchar_t *startup_path);
static BOOL bundle_name_conflicts_with_bundled_file(const wchar_t *bundle_name, const FileList *files);
static BOOL get_state_file_path(wchar_t *buffer, size_t buffer_count);
static void load_ui_state(void);
static void save_ui_state(void);
static BOOL path_exists(const wchar_t *path);
static BOOL path_has_extension(const wchar_t *path, const wchar_t *extension);
static BOOL path_is_directory(const wchar_t *path);
static BOOL path_is_relative(const wchar_t *path);
static BOOL path_is_within_directory(const wchar_t *path, const wchar_t *directory);
static BOOL build_path_within_directory(const wchar_t *directory, const wchar_t *relative_path, wchar_t *buffer, size_t buffer_count);
static uint32_t get_builder_compression_mode(void);
static void set_builder_compression_mode(uint32_t compression_mode);
static void free_path_list(PathList *list);
static BOOL add_unique_path(PathList *list, const wchar_t *path);
static BOOL get_temp_extract_directory(wchar_t *buffer, size_t buffer_count);
static BOOL read_manifest_header(FILE *file, const BundleFooter *footer, uint32_t *file_count_out, uint32_t *startup_len_out, uint32_t *runtime_options_out);
static void close_process_handles(PROCESS_INFORMATION *process_info);
static BOOL u64_fits_size_t(uint64_t value);
static BOOL extract_manifest_entry(FILE *bundle_file, const wchar_t *target_dir, const ManifestEntry *entry, PathList *created_dirs);
static BOOL write_bundle_manifest(FILE *out_file, const FileList *files, const char *startup_utf8, uint32_t runtime_options, uint64_t *manifest_offset_out);
static BOOL write_bundle_footer(FILE *out_file, uint64_t manifest_offset);
static int app_main(HINSTANCE instance);

static wchar_t *dup_wstr(const wchar_t *text) {
    size_t len = wcslen(text) + 1;
    wchar_t *copy = (wchar_t *)calloc(len, sizeof(wchar_t));
    if (copy) {
        memcpy(copy, text, len * sizeof(wchar_t));
    }
    return copy;
}

static char *utf8_from_wide(const wchar_t *text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    char *buffer;
    if (size <= 0) {
        return NULL;
    }
    buffer = (char *)malloc((size_t)size);
    if (!buffer) {
        return NULL;
    }
    if (!WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, size, NULL, NULL)) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static wchar_t *wide_from_utf8(const char *text) {
    int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *buffer;
    if (size <= 0) {
        return NULL;
    }
    buffer = (wchar_t *)calloc((size_t)size, sizeof(wchar_t));
    if (!buffer) {
        return NULL;
    }
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, buffer, size)) {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static void free_file_list(FileList *list) {
    size_t i;
    for (i = 0; i < list->count; ++i) {
        free(list->items[i].full_path);
        free(list->items[i].relative_utf8);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static BOOL add_file_entry(FileList *list, const wchar_t *full_path, const wchar_t *relative_path, uint64_t size) {
    FileEntry *new_items;
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2 : 32;
        new_items = (FileEntry *)realloc(list->items, new_capacity * sizeof(FileEntry));
        if (!new_items) {
            return FALSE;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count].full_path = dup_wstr(full_path);
    list->items[list->count].relative_utf8 = utf8_from_wide(relative_path);
    list->items[list->count].size = size;
    list->items[list->count].data_offset = 0;
    if (!list->items[list->count].full_path || !list->items[list->count].relative_utf8) {
        free(list->items[list->count].full_path);
        free(list->items[list->count].relative_utf8);
        return FALSE;
    }
    list->count += 1;
    return TRUE;
}

static BOOL get_file_size_u64(const wchar_t *path, uint64_t *size_out) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);
    *size_out = (uint64_t)size.QuadPart;
    return TRUE;
}

static BOOL enumerate_folder_recursive(const wchar_t *root, const wchar_t *current, FileList *list) {
    wchar_t search_path[PATH_BUFFER_CHARS];
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle;
    swprintf(search_path, _countof(search_path), L"%ls\\*", current);
    find_handle = FindFirstFileW(search_path, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    do {
        wchar_t full_path[PATH_BUFFER_CHARS];
        if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
            continue;
        }
        swprintf(full_path, _countof(full_path), L"%ls\\%ls", current, find_data.cFileName);
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!enumerate_folder_recursive(root, full_path, list)) {
                FindClose(find_handle);
                return FALSE;
            }
        } else {
            uint64_t size;
            const wchar_t *relative = full_path + wcslen(root);
            while (*relative == L'\\' || *relative == L'/') {
                relative++;
            }
            if (!get_file_size_u64(full_path, &size)) {
                FindClose(find_handle);
                return FALSE;
            }
            if (!add_file_entry(list, full_path, relative, size)) {
                FindClose(find_handle);
                return FALSE;
            }
        }
    } while (FindNextFileW(find_handle, &find_data));
    FindClose(find_handle);
    return TRUE;
}

static void free_buffer(Buffer *buffer) {
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
}

static void free_path_list(PathList *list) {
    size_t i;
    for (i = 0; i < list->count; ++i) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static BOOL add_unique_path(PathList *list, const wchar_t *path) {
    size_t i;
    wchar_t **new_items;
    wchar_t *copy;

    for (i = 0; i < list->count; ++i) {
        if (_wcsicmp(list->items[i], path) == 0) {
            return TRUE;
        }
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2 : 16;
        new_items = (wchar_t **)realloc(list->items, new_capacity * sizeof(wchar_t *));
        if (!new_items) {
            return FALSE;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    copy = dup_wstr(path);
    if (!copy) {
        return FALSE;
    }
    list->items[list->count++] = copy;
    return TRUE;
}

static BOOL get_temp_extract_directory(wchar_t *buffer, size_t buffer_count) {
    wchar_t temp_root[PATH_BUFFER_CHARS];
    wchar_t temp_dir[PATH_BUFFER_CHARS];
    DWORD temp_root_len;

    if (!buffer || buffer_count == 0) {
        return FALSE;
    }

    temp_root_len = GetTempPathW((DWORD)_countof(temp_root), temp_root);
    if (temp_root_len == 0 || temp_root_len >= _countof(temp_root)) {
        return FALSE;
    }
    if (!GetTempFileNameW(temp_root, L"fbd", 0, temp_dir)) {
        return FALSE;
    }
    SetFileAttributesW(temp_dir, FILE_ATTRIBUTE_NORMAL);
    if (!DeleteFileW(temp_dir)) {
        return FALSE;
    }
    if (!CreateDirectoryW(temp_dir, NULL)) {
        return FALSE;
    }

    wcsncpy(buffer, temp_dir, buffer_count - 1);
    buffer[buffer_count - 1] = L'\0';
    return TRUE;
}

static BOOL read_entire_file(const wchar_t *path, Buffer *buffer) {
    FILE *src = _wfopen(path, L"rb");
    uint64_t file_size;
    if (!src) {
        return FALSE;
    }
    if (!get_file_size_u64(path, &file_size) || file_size > SIZE_MAX) {
        fclose(src);
        return FALSE;
    }
    buffer->data = (unsigned char *)malloc((size_t)file_size ? (size_t)file_size : 1);
    if (!buffer->data) {
        fclose(src);
        return FALSE;
    }
    buffer->size = (size_t)file_size;
    if (buffer->size > 0 && fread(buffer->data, 1, buffer->size, src) != buffer->size) {
        free_buffer(buffer);
        fclose(src);
        return FALSE;
    }
    fclose(src);
    return TRUE;
}

static BOOL write_buffer(FILE *dst, const unsigned char *data, size_t size) {
    return size == 0 || fwrite(data, 1, size, dst) == size;
}

static BOOL get_stream_position_u64(FILE *file, uint64_t *position_out) {
    fpos_t position;
    if (fgetpos(file, &position) != 0) {
        return FALSE;
    }
    *position_out = (uint64_t)position;
    return TRUE;
}

static BOOL compression_api_compress(DWORD algorithm, const unsigned char *input, size_t input_size, Buffer *output) {
    COMPRESSOR_HANDLE compressor = NULL;
    SIZE_T compressed_size = 0;
    BOOL success = FALSE;

    if (!CreateCompressor(algorithm, NULL, &compressor)) {
        return FALSE;
    }

    success = Compress(compressor, (PVOID)input, input_size, NULL, 0, &compressed_size);
    if (success || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        CloseCompressor(compressor);
        return FALSE;
    }

    output->data = (unsigned char *)malloc(compressed_size > 0 ? compressed_size : 1);
    if (!output->data) {
        CloseCompressor(compressor);
        return FALSE;
    }

    success = Compress(compressor, (PVOID)input, input_size, output->data, compressed_size, &compressed_size);
    CloseCompressor(compressor);
    if (!success) {
        free_buffer(output);
        return FALSE;
    }

    output->size = compressed_size;
    return TRUE;
}

static BOOL compression_api_decompress(DWORD algorithm, const unsigned char *input, size_t input_size, unsigned char *output, size_t output_size) {
    DECOMPRESSOR_HANDLE decompressor = NULL;
    SIZE_T decompressed_size = 0;
    BOOL success;

    if (!CreateDecompressor(algorithm, NULL, &decompressor)) {
        return FALSE;
    }

    success = Decompress(decompressor, (PVOID)input, input_size, output, output_size, &decompressed_size);
    CloseDecompressor(decompressor);
    return success && decompressed_size == output_size;
}

static BOOL pack_file_data(const wchar_t *path, uint32_t requested_compression_mode, Buffer *stored, uint32_t *compression_type_out) {
    Buffer original = {0};
    Buffer compressed = {0};
    DWORD algorithm = 0;
    uint32_t compressed_type = BUNDLE_COMPRESSION_NONE;
    if (!read_entire_file(path, &original)) {
        return FALSE;
    }

    if (original.size == 0 || requested_compression_mode == BUILDER_COMPRESSION_STORE) {
        *compression_type_out = BUNDLE_COMPRESSION_NONE;
        *stored = original;
        return TRUE;
    }

    if (requested_compression_mode == BUILDER_COMPRESSION_XPRESS) {
        algorithm = COMPRESS_ALGORITHM_XPRESS;
        compressed_type = BUNDLE_COMPRESSION_XPRESS;
    } else {
        algorithm = COMPRESS_ALGORITHM_XPRESS_HUFF;
        compressed_type = BUNDLE_COMPRESSION_XPRESS_HUFF;
    }

    if (!compression_api_compress(algorithm, original.data, original.size, &compressed)) {
        free_buffer(&original);
        return FALSE;
    }
    if (compressed.size < original.size) {
        *compression_type_out = compressed_type;
        *stored = compressed;
        free_buffer(&original);
    } else {
        *compression_type_out = BUNDLE_COMPRESSION_NONE;
        *stored = original;
        free_buffer(&compressed);
    }
    return TRUE;
}

static const wchar_t *compression_type_name(uint32_t compression_type) {
    switch (compression_type) {
        case BUNDLE_COMPRESSION_NONE:
            return L"raw";
        case BUNDLE_COMPRESSION_XPRESS:
            return L"XPRESS";
        case BUNDLE_COMPRESSION_XPRESS_HUFF:
            return L"XPRESS_HUFF";
        default:
            return L"unknown";
    }
}

static void set_status(HWND edit, const wchar_t *text) {
    SetWindowTextW(edit, text);
}

static void append_status(HWND edit, const wchar_t *format, ...) {
    wchar_t message[STATUS_BUFFER_CHARS];
    wchar_t *existing;
    int existing_len;
    va_list args;
    va_start(args, format);
    _vsnwprintf(message, _countof(message), format, args);
    message[_countof(message) - 1] = L'\0';
    va_end(args);

    existing_len = GetWindowTextLengthW(edit);
    existing = (wchar_t *)calloc((size_t)existing_len + wcslen(message) + 4, sizeof(wchar_t));
    if (!existing) {
        return;
    }
    GetWindowTextW(edit, existing, existing_len + 1);
    if (existing_len > 0) {
        wcscat(existing, L"\r\n");
    }
    wcscat(existing, message);
    SetWindowTextW(edit, existing);
    free(existing);
}

static BOOL path_exists(const wchar_t *path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL path_has_extension(const wchar_t *path, const wchar_t *extension) {
    const wchar_t *dot = wcsrchr(path, L'.');
    return dot != NULL && _wcsicmp(dot, extension) == 0;
}

static BOOL path_is_directory(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static BOOL path_is_relative(const wchar_t *path) {
    return path[0] == L'\0' ||
        (wcsstr(path, L":") == NULL && wcsncmp(path, L"\\", 1) != 0 && wcsncmp(path, L"/", 1) != 0);
}

static BOOL path_is_within_directory(const wchar_t *path, const wchar_t *directory) {
    wchar_t full_path[PATH_BUFFER_CHARS];
    wchar_t full_directory[PATH_BUFFER_CHARS];
    size_t directory_len;

    if (!GetFullPathNameW(path, (DWORD)_countof(full_path), full_path, NULL) ||
        !GetFullPathNameW(directory, (DWORD)_countof(full_directory), full_directory, NULL)) {
        return FALSE;
    }

    directory_len = wcslen(full_directory);
    while (directory_len > 0 &&
        (full_directory[directory_len - 1] == L'\\' || full_directory[directory_len - 1] == L'/')) {
        full_directory[--directory_len] = L'\0';
    }

    if (_wcsnicmp(full_path, full_directory, directory_len) != 0) {
        return FALSE;
    }
    return full_path[directory_len] == L'\\' || full_path[directory_len] == L'/' || full_path[directory_len] == L'\0';
}

static BOOL build_path_within_directory(const wchar_t *directory, const wchar_t *relative_path, wchar_t *buffer, size_t buffer_count) {
    wchar_t combined[PATH_BUFFER_CHARS];

    if (!directory || !relative_path || !buffer || buffer_count == 0) {
        return FALSE;
    }
    swprintf(combined, _countof(combined), L"%ls\\%ls", directory, relative_path);
    if (!GetFullPathNameW(combined, (DWORD)buffer_count, buffer, NULL)) {
        return FALSE;
    }
    return path_is_within_directory(buffer, directory);
}

static uint32_t get_builder_compression_mode(void) {
    if (SendMessageW(g_ui.compression_store_radio, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        return BUILDER_COMPRESSION_STORE;
    }
    if (SendMessageW(g_ui.compression_xpress_radio, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        return BUILDER_COMPRESSION_XPRESS;
    }
    return BUILDER_COMPRESSION_XPRESS_HUFF;
}

static void set_builder_compression_mode(uint32_t compression_mode) {
    SendMessageW(g_ui.compression_store_radio, BM_SETCHECK,
        compression_mode == BUILDER_COMPRESSION_STORE ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_ui.compression_xpress_radio, BM_SETCHECK,
        compression_mode == BUILDER_COMPRESSION_XPRESS ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g_ui.compression_xpress_huff_radio, BM_SETCHECK,
        compression_mode == BUILDER_COMPRESSION_XPRESS_HUFF ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void close_process_handles(PROCESS_INFORMATION *process_info) {
    if (process_info->hThread) {
        CloseHandle(process_info->hThread);
        process_info->hThread = NULL;
    }
    if (process_info->hProcess) {
        CloseHandle(process_info->hProcess);
        process_info->hProcess = NULL;
    }
}

static BOOL u64_fits_size_t(uint64_t value) {
    return value <= (uint64_t)SIZE_MAX;
}

static BOOL ensure_parent_directories(const wchar_t *path, PathList *created_dirs) {
    wchar_t buffer[PATH_BUFFER_CHARS];
    wchar_t *scan;
    wcscpy(buffer, path);
    for (scan = buffer; *scan; ++scan) {
        if (*scan == L'\\' || *scan == L'/') {
            wchar_t saved = *scan;
            *scan = L'\0';
            if (wcslen(buffer) > 0 && buffer[wcslen(buffer) - 1] != L':') {
                if (CreateDirectoryW(buffer, NULL)) {
                    if (created_dirs && !add_unique_path(created_dirs, buffer)) {
                        return FALSE;
                    }
                } else if (GetLastError() != ERROR_ALREADY_EXISTS) {
                    return FALSE;
                }
            }
            *scan = saved;
        }
    }
    return TRUE;
}

static void free_manifest_entries(ManifestEntry *entries, uint32_t count) {
    uint32_t i;
    if (!entries) {
        return;
    }
    for (i = 0; i < count; ++i) {
        free(entries[i].relative_path);
    }
    free(entries);
}

static BOOL read_manifest_header(FILE *file, const BundleFooter *footer, uint32_t *file_count_out, uint32_t *startup_len_out, uint32_t *runtime_options_out) {
    uint32_t version = 0;
    uint32_t file_count = 0;
    uint32_t startup_len = 0;
    uint32_t runtime_options = 0;

    if (_fseeki64(file, (int64_t)footer->manifest_offset, SEEK_SET) != 0) {
        return FALSE;
    }
    if (fread(&version, sizeof(version), 1, file) != 1 || version != BUNDLE_VERSION) {
        return FALSE;
    }
    if (fread(&file_count, sizeof(file_count), 1, file) != 1) {
        return FALSE;
    }
    if (fread(&startup_len, sizeof(startup_len), 1, file) != 1) {
        return FALSE;
    }
    if (fread(&runtime_options, sizeof(runtime_options), 1, file) != 1) {
        return FALSE;
    }
    if (file_count_out) {
        *file_count_out = file_count;
    }
    if (startup_len_out) {
        *startup_len_out = startup_len;
    }
    if (runtime_options_out) {
        *runtime_options_out = runtime_options;
    }
    return TRUE;
}

static BOOL read_bundle_runtime_options(const wchar_t *exe_path, const BundleFooter *footer, uint32_t *runtime_options_out) {
    FILE *file;
    BOOL success;

    file = _wfopen(exe_path, L"rb");
    if (!file) {
        return FALSE;
    }
    success = read_manifest_header(file, footer, NULL, NULL, runtime_options_out);
    fclose(file);
    return success;
}

static BOOL read_manifest_entries(
    FILE *file,
    const BundleFooter *footer,
    wchar_t **startup_path_out,
    uint32_t *runtime_options_out,
    ManifestEntry **entries_out,
    uint32_t *file_count_out
) {
    uint32_t file_count = 0;
    uint32_t startup_len = 0;
    uint32_t i;
    ManifestEntry *entries = NULL;

    if (!read_manifest_header(file, footer, &file_count, &startup_len, runtime_options_out)) {
        return FALSE;
    }
    if (startup_len > 0 && startup_path_out) {
        char *startup_utf8 = (char *)malloc((size_t)startup_len + 1);
        if (!startup_utf8) {
            return FALSE;
        }
        if (fread(startup_utf8, 1, startup_len, file) != startup_len) {
            free(startup_utf8);
            return FALSE;
        }
        startup_utf8[startup_len] = '\0';
        *startup_path_out = wide_from_utf8(startup_utf8);
        free(startup_utf8);
        if (!*startup_path_out) {
            return FALSE;
        }
    } else if (startup_len > 0) {
        if (_fseeki64(file, startup_len, SEEK_CUR) != 0) {
            return FALSE;
        }
    }

    entries = (ManifestEntry *)calloc(file_count, sizeof(ManifestEntry));
    if (file_count > 0 && !entries) {
        return FALSE;
    }

    for (i = 0; i < file_count; ++i) {
        uint32_t path_len = 0;
        char *relative_utf8;
        if (fread(&path_len, sizeof(path_len), 1, file) != 1 ||
            fread(&entries[i].data_offset, sizeof(entries[i].data_offset), 1, file) != 1 ||
            fread(&entries[i].file_size, sizeof(entries[i].file_size), 1, file) != 1 ||
            fread(&entries[i].stored_size, sizeof(entries[i].stored_size), 1, file) != 1 ||
            fread(&entries[i].compression_type, sizeof(entries[i].compression_type), 1, file) != 1) {
            free_manifest_entries(entries, file_count);
            return FALSE;
        }
        relative_utf8 = (char *)malloc((size_t)path_len + 1);
        if (!relative_utf8) {
            free_manifest_entries(entries, file_count);
            return FALSE;
        }
        if (fread(relative_utf8, 1, path_len, file) != path_len) {
            free(relative_utf8);
            free_manifest_entries(entries, file_count);
            return FALSE;
        }
        relative_utf8[path_len] = '\0';
        entries[i].relative_path = wide_from_utf8(relative_utf8);
        free(relative_utf8);
        if (!entries[i].relative_path) {
            free_manifest_entries(entries, file_count);
            return FALSE;
        }
    }

    *entries_out = entries;
    *file_count_out = file_count;
    return TRUE;
}

static BOOL extract_manifest_entry(FILE *bundle_file, const wchar_t *target_dir, const ManifestEntry *entry, PathList *created_dirs) {
    wchar_t output_path[PATH_BUFFER_CHARS];
    FILE *output_file = NULL;
    Buffer stored = {0};
    unsigned char *final_data = NULL;
    size_t file_size;
    size_t stored_size;
    BOOL success = FALSE;

    if (!u64_fits_size_t(entry->file_size) || !u64_fits_size_t(entry->stored_size)) {
        return FALSE;
    }
    file_size = (size_t)entry->file_size;
    stored_size = (size_t)entry->stored_size;
    /* Uncompressed entries must be byte-for-byte copies of the original data.
       Reject mismatched sizes before writing anything out. */
    if (entry->compression_type == BUNDLE_COMPRESSION_NONE && stored_size != file_size) {
        return FALSE;
    }

    if (!build_path_within_directory(target_dir, entry->relative_path, output_path, _countof(output_path))) {
        return FALSE;
    }
    if (!ensure_parent_directories(output_path, created_dirs)) {
        goto cleanup;
    }

    output_file = _wfopen(output_path, L"wb");
    if (!output_file) {
        goto cleanup;
    }
    if (_fseeki64(bundle_file, (int64_t)entry->data_offset, SEEK_SET) != 0) {
        goto cleanup;
    }

    stored.size = stored_size;
    stored.data = (unsigned char *)malloc(stored.size > 0 ? stored.size : 1);
    if (!stored.data) {
        goto cleanup;
    }
    if (stored.size > 0 && fread(stored.data, 1, stored.size, bundle_file) != stored.size) {
        goto cleanup;
    }

    if (entry->compression_type == BUNDLE_COMPRESSION_NONE) {
        final_data = stored.data;
    } else if (entry->compression_type == BUNDLE_COMPRESSION_XPRESS) {
        final_data = (unsigned char *)malloc(file_size > 0 ? file_size : 1);
        if (!final_data || !compression_api_decompress(COMPRESS_ALGORITHM_XPRESS, stored.data, stored.size, final_data, file_size)) {
            goto cleanup;
        }
    } else if (entry->compression_type == BUNDLE_COMPRESSION_XPRESS_HUFF) {
        final_data = (unsigned char *)malloc(file_size > 0 ? file_size : 1);
        if (!final_data || !compression_api_decompress(COMPRESS_ALGORITHM_XPRESS_HUFF, stored.data, stored.size, final_data, file_size)) {
            goto cleanup;
        }
    } else {
        goto cleanup;
    }

    success = write_buffer(output_file, final_data, file_size);

cleanup:
    if (entry->compression_type == BUNDLE_COMPRESSION_XPRESS ||
        entry->compression_type == BUNDLE_COMPRESSION_XPRESS_HUFF) {
        free(final_data);
    }
    free_buffer(&stored);
    if (output_file) {
        fclose(output_file);
    }
    return success;
}

static BOOL extract_bundle_to_directory(const wchar_t *exe_path, const wchar_t *target_dir, const BundleFooter *footer, wchar_t **startup_path_out, uint32_t *runtime_options_out, PathList *created_dirs) {
    FILE *file = NULL;
    uint32_t file_count = 0;
    uint32_t i;
    ManifestEntry *entries = NULL;
    BOOL success = FALSE;

    file = _wfopen(exe_path, L"rb");
    if (!file) {
        return FALSE;
    }
    if (!read_manifest_entries(file, footer, startup_path_out, runtime_options_out, &entries, &file_count)) {
        goto cleanup;
    }

    success = TRUE;
    for (i = 0; i < file_count; ++i) {
        if (!extract_manifest_entry(file, target_dir, &entries[i], created_dirs)) {
            success = FALSE;
            break;
        }
    }

cleanup:
    free_manifest_entries(entries, file_count);
    if (file) {
        fclose(file);
    }
    return success;
}

static void delete_directory_tree_recursive(const wchar_t *dir_path) {
    wchar_t search_path[PATH_BUFFER_CHARS];
    WIN32_FIND_DATAW find_data;
    HANDLE find_handle;

    swprintf(search_path, _countof(search_path), L"%ls\\*", dir_path);
    find_handle = FindFirstFileW(search_path, &find_data);
    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            wchar_t child_path[PATH_BUFFER_CHARS];
            if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
                continue;
            }
            swprintf(child_path, _countof(child_path), L"%ls\\%ls", dir_path, find_data.cFileName);
            if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_directory_tree_recursive(child_path);
            } else {
                SetFileAttributesW(child_path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child_path);
            }
        } while (FindNextFileW(find_handle, &find_data));
        FindClose(find_handle);
    }
    SetFileAttributesW(dir_path, FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryW(dir_path);
}

static void cleanup_extracted_files(const wchar_t *exe_path, const wchar_t *target_dir, const BundleFooter *footer, const PathList *created_dirs) {
    FILE *file = _wfopen(exe_path, L"rb");
    ManifestEntry *entries = NULL;
    uint32_t file_count = 0;
    uint32_t i;
    if (!file) {
        return;
    }
    if (!read_manifest_entries(file, footer, NULL, NULL, &entries, &file_count)) {
        fclose(file);
        return;
    }
    fclose(file);
    for (i = 0; i < file_count; ++i) {
        wchar_t output_path[MAX_PATH * 4];
        if (!build_path_within_directory(target_dir, entries[i].relative_path, output_path, _countof(output_path))) {
            continue;
        }
        SetFileAttributesW(output_path, FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(output_path);
    }
    if (created_dirs) {
        for (i = (uint32_t)created_dirs->count; i > 0; --i) {
            if (path_is_directory(created_dirs->items[i - 1])) {
                delete_directory_tree_recursive(created_dirs->items[i - 1]);
            }
        }
    }
    free_manifest_entries(entries, file_count);
}

static BOOL read_bundle_footer(const wchar_t *exe_path, BundleFooter *footer_out) {
    FILE *file = _wfopen(exe_path, L"rb");
    BundleFooter footer;
    int match;
    if (!file) {
        return FALSE;
    }
    if (_fseeki64(file, -((int64_t)sizeof(BundleFooter)), SEEK_END) != 0) {
        fclose(file);
        return FALSE;
    }
    if (fread(&footer, sizeof(footer), 1, file) != 1) {
        fclose(file);
        return FALSE;
    }
    fclose(file);
    match = memcmp(footer.magic, BUNDLE_MAGIC, 8) == 0;
    if (!match || footer.version != BUNDLE_VERSION) {
        return FALSE;
    }
    *footer_out = footer;
    return TRUE;
}

static BOOL CALLBACK find_first_group_icon_name(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR parameter) {
    ResourceNameSelection *selection = (ResourceNameSelection *)parameter;
    (void)module;
    (void)type;

    selection->found = TRUE;
    if (IS_INTRESOURCE(name)) {
        selection->is_integer = TRUE;
        selection->id = (WORD)(ULONG_PTR)name;
        selection->name[0] = L'\0';
    } else {
        selection->is_integer = FALSE;
        wcsncpy(selection->name, name, _countof(selection->name) - 1);
        selection->name[_countof(selection->name) - 1] = L'\0';
    }
    return FALSE;
}

static BOOL apply_icon_group_to_exe(const wchar_t *exe_path, const wchar_t *icon_exe_path) {
    HMODULE source_module = NULL;
    HANDLE update = NULL;
    ResourceNameSelection selection = {0};
    LPCWSTR group_name = MAKEINTRESOURCEW(1);
    HRSRC group_resource;
    HGLOBAL group_handle;
    const BYTE *group_data;
    DWORD group_size;
    const GrpIconDirHeader *group_header;
    const GrpIconDirEntry *group_entries;
    BYTE *new_group_data = NULL;
    GrpIconDirHeader *new_group_header;
    GrpIconDirEntry *new_group_entries;
    UINT i;
    BOOL success = FALSE;

    source_module = LoadLibraryExW(icon_exe_path, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!source_module) {
        return FALSE;
    }

    group_resource = FindResourceW(source_module, group_name, RT_GROUP_ICON);
    if (!group_resource) {
        EnumResourceNamesW(source_module, RT_GROUP_ICON, find_first_group_icon_name, (LONG_PTR)&selection);
        if (!selection.found) {
            goto cleanup;
        }
        group_name = selection.is_integer ? MAKEINTRESOURCEW(selection.id) : selection.name;
        group_resource = FindResourceW(source_module, group_name, RT_GROUP_ICON);
        if (!group_resource) {
            goto cleanup;
        }
    }

    group_size = SizeofResource(source_module, group_resource);
    if (group_size < sizeof(GrpIconDirHeader)) {
        goto cleanup;
    }

    group_handle = LoadResource(source_module, group_resource);
    group_data = group_handle ? (const BYTE *)LockResource(group_handle) : NULL;
    if (!group_data) {
        goto cleanup;
    }

    group_header = (const GrpIconDirHeader *)group_data;
    if (group_header->idReserved != 0 || group_header->idType != 1 || group_header->idCount == 0) {
        goto cleanup;
    }
    if (group_size < sizeof(GrpIconDirHeader) + group_header->idCount * sizeof(GrpIconDirEntry)) {
        goto cleanup;
    }

    new_group_data = (BYTE *)malloc(sizeof(GrpIconDirHeader) + group_header->idCount * sizeof(GrpIconDirEntry));
    if (!new_group_data) {
        goto cleanup;
    }

    new_group_header = (GrpIconDirHeader *)new_group_data;
    new_group_entries = (GrpIconDirEntry *)(new_group_data + sizeof(GrpIconDirHeader));
    new_group_header->idReserved = group_header->idReserved;
    new_group_header->idType = group_header->idType;
    new_group_header->idCount = group_header->idCount;
    group_entries = (const GrpIconDirEntry *)(group_data + sizeof(GrpIconDirHeader));

    update = BeginUpdateResourceW(exe_path, FALSE);
    if (!update) {
        goto cleanup;
    }

    for (i = 0; i < group_header->idCount; ++i) {
        HRSRC icon_resource = FindResourceW(source_module, MAKEINTRESOURCEW(group_entries[i].nID), RT_ICON);
        HGLOBAL icon_handle;
        const BYTE *icon_data;
        DWORD icon_size;

        if (!icon_resource) {
            EndUpdateResourceW(update, TRUE);
            update = NULL;
            goto cleanup;
        }

        icon_size = SizeofResource(source_module, icon_resource);
        if (icon_size == 0) {
            EndUpdateResourceW(update, TRUE);
            update = NULL;
            goto cleanup;
        }

        icon_handle = LoadResource(source_module, icon_resource);
        icon_data = icon_handle ? (const BYTE *)LockResource(icon_handle) : NULL;
        if (!icon_data || icon_size != group_entries[i].dwBytesInRes) {
            EndUpdateResourceW(update, TRUE);
            update = NULL;
            goto cleanup;
        }

        new_group_entries[i] = group_entries[i];
        new_group_entries[i].nID = (WORD)(i + 1);
        if (!UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(i + 1),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), (LPVOID)icon_data, icon_size)) {
            EndUpdateResourceW(update, TRUE);
            update = NULL;
            goto cleanup;
        }
    }

    if (!UpdateResourceW(update, RT_GROUP_ICON, MAKEINTRESOURCEW(1),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
            new_group_data,
            sizeof(GrpIconDirHeader) + group_header->idCount * sizeof(GrpIconDirEntry))) {
        EndUpdateResourceW(update, TRUE);
        update = NULL;
        goto cleanup;
    }

    if (!EndUpdateResourceW(update, FALSE)) {
        update = NULL;
        goto cleanup;
    }
    update = NULL;
    success = TRUE;

cleanup:
    if (update) {
        EndUpdateResourceW(update, TRUE);
    }
    free(new_group_data);
    if (source_module) {
        FreeLibrary(source_module);
    }
    return success;
}

static BOOL write_bundle_manifest(FILE *out_file, const FileList *files, const char *startup_utf8, uint32_t runtime_options, uint64_t *manifest_offset_out) {
    uint32_t version = BUNDLE_VERSION;
    uint32_t file_count = (uint32_t)files->count;
    uint32_t startup_len = startup_utf8 ? (uint32_t)strlen(startup_utf8) : 0;
    size_t i;

    /* Payload bytes are already appended at this point; the manifest offset is
       what lets the launcher jump straight to the metadata from the footer. */
    if (!get_stream_position_u64(out_file, manifest_offset_out)) {
        return FALSE;
    }

    if (fwrite(&version, sizeof(version), 1, out_file) != 1 ||
        fwrite(&file_count, sizeof(file_count), 1, out_file) != 1 ||
        fwrite(&startup_len, sizeof(startup_len), 1, out_file) != 1 ||
        fwrite(&runtime_options, sizeof(runtime_options), 1, out_file) != 1) {
        return FALSE;
    }
    if (startup_len > 0 && fwrite(startup_utf8, 1, startup_len, out_file) != startup_len) {
        return FALSE;
    }

    for (i = 0; i < files->count; ++i) {
        const FileEntry *entry = &files->items[i];
        uint32_t path_len = (uint32_t)strlen(entry->relative_utf8);

        if (fwrite(&path_len, sizeof(path_len), 1, out_file) != 1 ||
            fwrite(&entry->data_offset, sizeof(entry->data_offset), 1, out_file) != 1 ||
            fwrite(&entry->size, sizeof(entry->size), 1, out_file) != 1 ||
            fwrite(&entry->stored_size, sizeof(entry->stored_size), 1, out_file) != 1 ||
            fwrite(&entry->compression_type, sizeof(entry->compression_type), 1, out_file) != 1 ||
            fwrite(entry->relative_utf8, 1, path_len, out_file) != path_len) {
            return FALSE;
        }
    }

    return TRUE;
}

static BOOL write_bundle_footer(FILE *out_file, uint64_t manifest_offset) {
    BundleFooter footer;

    memcpy(footer.magic, BUNDLE_MAGIC, sizeof(footer.magic));
    footer.version = BUNDLE_VERSION;
    footer.manifest_offset = manifest_offset;

    return fwrite(&footer, sizeof(footer), 1, out_file) == 1;
}

static BOOL run_bundled_mode(HINSTANCE instance) {
    wchar_t exe_path[PATH_BUFFER_CHARS];
    wchar_t extract_dir[PATH_BUFFER_CHARS];
    wchar_t *startup_relative = NULL;
    wchar_t startup_full[PATH_BUFFER_CHARS];
    BundleFooter footer;
    PathList created_dirs = {0};
    uint32_t runtime_options = 0;
    BOOL keep_files = FALSE;
    BOOL extract_to_temp = FALSE;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t *last_slash;
    (void)instance;
    if (!GetModuleFileNameW(NULL, exe_path, _countof(exe_path))) {
        return FALSE;
    }
    if (!read_bundle_footer(exe_path, &footer)) {
        return FALSE;
    }
    if (!read_bundle_runtime_options(exe_path, &footer, &runtime_options)) {
        return FALSE;
    }
    keep_files = (runtime_options & BUNDLE_OPTION_KEEP_FILES) != 0;
    extract_to_temp = (runtime_options & BUNDLE_OPTION_EXTRACT_TO_TEMP) != 0;

    if (extract_to_temp) {
        if (!get_temp_extract_directory(extract_dir, _countof(extract_dir))) {
            MessageBoxW(NULL, L"Could not create a temp extraction folder.", APP_TITLE, MB_ICONERROR);
            return TRUE;
        }
    } else {
        wcscpy(extract_dir, exe_path);
        last_slash = wcsrchr(extract_dir, L'\\');
        if (!last_slash) {
            MessageBoxW(NULL, L"Could not resolve the bundle folder.", APP_TITLE, MB_ICONERROR);
            return TRUE;
        }
        *last_slash = L'\0';
    }

    if (!extract_bundle_to_directory(exe_path, extract_dir, &footer, &startup_relative, &runtime_options, &created_dirs)) {
        MessageBoxW(NULL, L"Extraction failed.", APP_TITLE, MB_ICONERROR);
        if (extract_to_temp && path_is_directory(extract_dir)) {
            delete_directory_tree_recursive(extract_dir);
        }
        free_path_list(&created_dirs);
        return TRUE;
    }
    keep_files = (runtime_options & BUNDLE_OPTION_KEEP_FILES) != 0;
    extract_to_temp = (runtime_options & BUNDLE_OPTION_EXTRACT_TO_TEMP) != 0;
    if (!startup_relative || startup_relative[0] == L'\0') {
        wchar_t message[MAX_PATH * 4];
        swprintf(message, _countof(message), L"Files were extracted to:\n%ls", extract_dir);
        MessageBoxW(NULL, message, APP_TITLE, MB_OK | MB_ICONINFORMATION);
        free(startup_relative);
        free_path_list(&created_dirs);
        return TRUE;
    }
    if (!build_path_within_directory(extract_dir, startup_relative, startup_full, _countof(startup_full))) {
        free(startup_relative);
        if (extract_to_temp && path_is_directory(extract_dir)) {
            delete_directory_tree_recursive(extract_dir);
        }
        free_path_list(&created_dirs);
        MessageBoxW(NULL, L"Startup EXE path is outside the extraction folder.", APP_TITLE, MB_ICONERROR);
        return TRUE;
    }
    free(startup_relative);
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessW(startup_full, NULL, NULL, NULL, FALSE, 0, NULL, extract_dir, &si, &pi)) {
        wchar_t message[PATH_BUFFER_CHARS];
        swprintf(message, _countof(message), L"Could not launch:\n%ls", startup_full);
        MessageBoxW(NULL, message, APP_TITLE, MB_ICONERROR);
        if (extract_to_temp && path_is_directory(extract_dir)) {
            delete_directory_tree_recursive(extract_dir);
        }
        free_path_list(&created_dirs);
        return TRUE;
    }
    if (keep_files) {
        close_process_handles(&pi);
        free_path_list(&created_dirs);
        return TRUE;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    close_process_handles(&pi);
    if (extract_to_temp) {
        delete_directory_tree_recursive(extract_dir);
    } else {
        cleanup_extracted_files(exe_path, extract_dir, &footer, &created_dirs);
    }
    free_path_list(&created_dirs);
    return TRUE;
}

static BOOL apply_icon_to_exe(const wchar_t *exe_path, const wchar_t *icon_path) {
    HANDLE file = CreateFileW(icon_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD size;
    BYTE *buffer;
    DWORD read_bytes;
    IconDirHeader *icon_header;
    IconDirEntry *icon_entries;
    BYTE *group_buffer;
    DWORD group_size;
    GrpIconDirHeader *group_header;
    GrpIconDirEntry *group_entries;
    HANDLE update;
    WORD i;
    if (file == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    size = GetFileSize(file, NULL);
    if (size < sizeof(IconDirHeader)) {
        CloseHandle(file);
        return FALSE;
    }
    buffer = (BYTE *)malloc(size);
    if (!buffer) {
        CloseHandle(file);
        return FALSE;
    }
    if (!ReadFile(file, buffer, size, &read_bytes, NULL) || read_bytes != size) {
        free(buffer);
        CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);

    icon_header = (IconDirHeader *)buffer;
    if (icon_header->idReserved != 0 || icon_header->idType != 1 || icon_header->idCount == 0) {
        free(buffer);
        return FALSE;
    }
    if (size < sizeof(IconDirHeader) + icon_header->idCount * sizeof(IconDirEntry)) {
        free(buffer);
        return FALSE;
    }

    icon_entries = (IconDirEntry *)(buffer + sizeof(IconDirHeader));
    group_size = sizeof(GrpIconDirHeader) + icon_header->idCount * sizeof(GrpIconDirEntry);
    group_buffer = (BYTE *)calloc(group_size, 1);
    if (!group_buffer) {
        free(buffer);
        return FALSE;
    }
    group_header = (GrpIconDirHeader *)group_buffer;
    group_entries = (GrpIconDirEntry *)(group_buffer + sizeof(GrpIconDirHeader));
    group_header->idReserved = 0;
    group_header->idType = 1;
    group_header->idCount = icon_header->idCount;

    update = BeginUpdateResourceW(exe_path, FALSE);
    if (!update) {
        free(group_buffer);
        free(buffer);
        return FALSE;
    }

    for (i = 0; i < icon_header->idCount; ++i) {
        BYTE *image_data = buffer + icon_entries[i].dwImageOffset;
        if (icon_entries[i].dwImageOffset + icon_entries[i].dwBytesInRes > size) {
            EndUpdateResourceW(update, TRUE);
            free(group_buffer);
            free(buffer);
            return FALSE;
        }
        group_entries[i].bWidth = icon_entries[i].bWidth;
        group_entries[i].bHeight = icon_entries[i].bHeight;
        group_entries[i].bColorCount = icon_entries[i].bColorCount;
        group_entries[i].bReserved = icon_entries[i].bReserved;
        group_entries[i].wPlanes = icon_entries[i].wPlanes;
        group_entries[i].wBitCount = icon_entries[i].wBitCount;
        group_entries[i].dwBytesInRes = icon_entries[i].dwBytesInRes;
        group_entries[i].nID = (WORD)(i + 1);
        if (!UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(i + 1),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), image_data, icon_entries[i].dwBytesInRes)) {
            EndUpdateResourceW(update, TRUE);
            free(group_buffer);
            free(buffer);
            return FALSE;
        }
    }

    if (!UpdateResourceW(update, RT_GROUP_ICON, MAKEINTRESOURCEW(1),
            MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), group_buffer, group_size)) {
        EndUpdateResourceW(update, TRUE);
        free(group_buffer);
        free(buffer);
        return FALSE;
    }

    if (!EndUpdateResourceW(update, FALSE)) {
        free(group_buffer);
        free(buffer);
        return FALSE;
    }

    free(group_buffer);
    free(buffer);
    return TRUE;
}

static BOOL apply_icon_source_to_exe(const wchar_t *exe_path, const wchar_t *icon_source_path) {
    if (path_has_extension(icon_source_path, L".exe")) {
        return apply_icon_group_to_exe(exe_path, icon_source_path);
    }
    if (path_has_extension(icon_source_path, L".ico")) {
        return apply_icon_to_exe(exe_path, icon_source_path);
    }
    return FALSE;
}

static BOOL build_bundle(HWND window) {
    wchar_t source[PATH_BUFFER_CHARS];
    wchar_t startup[PATH_BUFFER_CHARS];
    wchar_t output_folder[PATH_BUFFER_CHARS];
    wchar_t output_name[PATH_BUFFER_CHARS];
    wchar_t output[PATH_BUFFER_CHARS];
    wchar_t icon[PATH_BUFFER_CHARS];
    wchar_t startup_full_path[PATH_BUFFER_CHARS];
    wchar_t inherited_icon_path[PATH_BUFFER_CHARS];
    const wchar_t *icon_source = NULL;
    wchar_t self_path[PATH_BUFFER_CHARS];
    FILE *out_file = NULL;
    FileList files = {0};
    size_t i;
    uint64_t manifest_offset;
    char *startup_utf8 = NULL;
    uint64_t total_original = 0;
    uint64_t total_stored = 0;
    uint32_t builder_compression_mode = BUILDER_COMPRESSION_XPRESS_HUFF;
    uint32_t runtime_options = 0;
    BOOL success = FALSE;

    GetWindowTextW(g_ui.source_edit, source, _countof(source));
    GetWindowTextW(g_ui.startup_edit, startup, _countof(startup));
    GetWindowTextW(g_ui.output_edit, output_folder, _countof(output_folder));
    GetWindowTextW(g_ui.output_name_edit, output_name, _countof(output_name));
    GetWindowTextW(g_ui.icon_edit, icon, _countof(icon));
    builder_compression_mode = get_builder_compression_mode();
    if (SendMessageW(g_ui.keep_files_check, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        runtime_options |= BUNDLE_OPTION_KEEP_FILES;
    }
    if (SendMessageW(g_ui.extract_to_temp_check, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        runtime_options |= BUNDLE_OPTION_EXTRACT_TO_TEMP;
    }

    set_status(g_ui.status_edit, L"");

    if (source[0] == L'\0' || output_folder[0] == L'\0' || output_name[0] == L'\0') {
        MessageBoxW(window, L"Pick a source folder, output folder, and bundle name first.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (!path_is_directory(source)) {
        MessageBoxW(window, L"The source folder does not exist.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (!path_is_directory(output_folder)) {
        MessageBoxW(window, L"The output folder does not exist.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (!bundle_name_is_valid(output_name)) {
        MessageBoxW(window, L"Bundle name cannot be empty or contain invalid filename characters.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (!path_is_relative(startup)) {
        MessageBoxW(window, L"Startup EXE must be a path relative to the source folder.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (startup[0] != L'\0') {
        swprintf(startup_full_path, _countof(startup_full_path), L"%ls\\%ls", source, startup);
        if (!path_is_within_directory(startup_full_path, source) ||
            !path_exists(startup_full_path) ||
            !path_has_extension(startup_full_path, L".exe")) {
            MessageBoxW(window, L"Startup EXE must point to an .exe inside the source folder.", APP_TITLE, MB_ICONWARNING);
            return FALSE;
        }
    }
    if (bundle_name_conflicts_with_startup_exe(output_name, startup)) {
        MessageBoxW(window, L"Bundle name cannot match the startup EXE name. Choose a different bundle name so the generated EXE does not conflict with the file it will run.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (icon[0] != L'\0' && !path_exists(icon)) {
        MessageBoxW(window, L"The icon source could not be found.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (icon[0] != L'\0' && !path_has_extension(icon, L".ico") && !path_has_extension(icon, L".exe")) {
        MessageBoxW(window, L"Icon source must be an .ico or .exe file.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }
    if (!GetModuleFileNameW(NULL, self_path, _countof(self_path))) {
        MessageBoxW(window, L"Could not locate the builder executable.", APP_TITLE, MB_ICONERROR);
        return FALSE;
    }
    swprintf(output, _countof(output), L"%ls\\%ls.exe", output_folder, output_name);
    if (path_is_within_directory(output, source)) {
        MessageBoxW(window, L"Output bundle must be outside the source folder. This prevents the generated EXE and custom icon update from overwriting bundled source files.", APP_TITLE, MB_ICONWARNING);
        return FALSE;
    }

    append_status(g_ui.status_edit, L"Scanning %ls", source);
    if (!enumerate_folder_recursive(source, source, &files)) {
        MessageBoxW(window, L"Could not enumerate the source folder.", APP_TITLE, MB_ICONERROR);
        goto cleanup;
    }
    if (files.count == 0) {
        MessageBoxW(window, L"The source folder does not contain any files.", APP_TITLE, MB_ICONWARNING);
        goto cleanup;
    }
    if (bundle_name_conflicts_with_bundled_file(output_name, &files)) {
        MessageBoxW(window, L"Bundle name cannot match a bundled root-level EXE filename. Choose a different bundle name so extraction does not overwrite the outer bundle EXE.", APP_TITLE, MB_ICONWARNING);
        goto cleanup;
    }
    append_status(g_ui.status_edit, L"Found %zu files", files.count);

    if (!ensure_parent_directories(output, NULL)) {
        MessageBoxW(window, L"Could not create the output folder path.", APP_TITLE, MB_ICONERROR);
        goto cleanup;
    }
    if (!CopyFileW(self_path, output, FALSE)) {
        MessageBoxW(window, L"Could not copy the builder EXE to the output location.", APP_TITLE, MB_ICONERROR);
        goto cleanup;
    }
    append_status(g_ui.status_edit, L"Created base bundle %ls", output);

    if (icon[0] != L'\0') {
        icon_source = icon;
    } else if (startup[0] != L'\0') {
        if (path_exists(startup_full_path) && path_has_extension(startup_full_path, L".exe")) {
            wcscpy(inherited_icon_path, startup_full_path);
            icon_source = inherited_icon_path;
        } else {
            append_status(g_ui.status_edit, L"Startup EXE icon inheritance skipped. Keeping the default bundle icon.");
        }
    }

    if (icon_source) {
        append_status(g_ui.status_edit, L"Applying icon from %ls", icon_source);
        if (!apply_icon_source_to_exe(output, icon_source)) {
            if (icon[0] != L'\0') {
                MessageBoxW(window, L"The icon source could not be applied to the bundle.", APP_TITLE, MB_ICONWARNING);
                goto cleanup;
            }
            append_status(g_ui.status_edit, L"Could not copy the startup EXE icon. Keeping the default bundle icon.");
        } else if (icon[0] == L'\0') {
            append_status(g_ui.status_edit, L"Bundle icon inherited from the startup EXE.");
        }
    }

    out_file = _wfopen(output, L"rb+");
    if (!out_file) {
        MessageBoxW(window, L"Could not open the output EXE for writing.", APP_TITLE, MB_ICONERROR);
        goto cleanup;
    }
    if (_fseeki64(out_file, 0, SEEK_END) != 0) {
        MessageBoxW(window, L"Could not seek to the end of the output EXE.", APP_TITLE, MB_ICONERROR);
        goto cleanup;
    }

    for (i = 0; i < files.count; ++i) {
        Buffer stored = {0};
        double saved_percent = 0.0;
        if (!get_stream_position_u64(out_file, &files.items[i].data_offset)) {
            MessageBoxW(window, L"Could not determine the output EXE write position.", APP_TITLE, MB_ICONERROR);
            goto cleanup;
        }
        if (!pack_file_data(files.items[i].full_path, builder_compression_mode, &stored, &files.items[i].compression_type)) {
            MessageBoxW(window, L"Failed while preparing bundled file data.", APP_TITLE, MB_ICONERROR);
            goto cleanup;
        }
        files.items[i].stored_size = (uint64_t)stored.size;
        total_original += files.items[i].size;
        total_stored += files.items[i].stored_size;
        if (files.items[i].size > 0 && files.items[i].stored_size <= files.items[i].size) {
            saved_percent = ((double)(files.items[i].size - files.items[i].stored_size) * 100.0) /
                (double)files.items[i].size;
        }
        append_status(g_ui.status_edit, L"Packing %S (%ls, %.1f%% saved)", files.items[i].relative_utf8,
            compression_type_name(files.items[i].compression_type), saved_percent);
        if (!write_buffer(out_file, stored.data, stored.size)) {
            free_buffer(&stored);
            MessageBoxW(window, L"Failed while writing bundled file data.", APP_TITLE, MB_ICONERROR);
            goto cleanup;
        }
        free_buffer(&stored);
    }

    if (startup[0] != L'\0') {
        startup_utf8 = utf8_from_wide(startup);
        if (!startup_utf8) {
            MessageBoxW(window, L"Could not encode the startup path.", APP_TITLE, MB_ICONERROR);
            goto cleanup;
        }
    }
    if (!write_bundle_manifest(out_file, &files, startup_utf8, runtime_options, &manifest_offset)) {
        MessageBoxW(window, L"Failed while writing the bundle manifest.", APP_TITLE, MB_ICONERROR);
        goto cleanup;
    }
    if (!write_bundle_footer(out_file, manifest_offset)) {
        MessageBoxW(window, L"Failed while writing the bundle footer.", APP_TITLE, MB_ICONERROR);
        goto cleanup;
    }

    append_status(g_ui.status_edit, L"Bundle complete. Stored %llu of %llu bytes.",
        (unsigned long long)total_stored, (unsigned long long)total_original);
    MessageBoxW(window, L"Bundle created successfully.", APP_TITLE, MB_OK | MB_ICONINFORMATION);
    success = TRUE;

cleanup:
    if (out_file) {
        fclose(out_file);
    }
    free(startup_utf8);
    free_file_list(&files);
    return success;
}

static BOOL pick_folder(HWND owner, wchar_t *buffer) {
    BROWSEINFOW bi = {0};
    LPITEMIDLIST pidl;
    wchar_t display[MAX_PATH];
    bi.hwndOwner = owner;
    bi.pszDisplayName = display;
    bi.lpszTitle = L"Choose a source folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    pidl = SHBrowseForFolderW(&bi);
    if (!pidl) {
        return FALSE;
    }
    if (!SHGetPathFromIDListW(pidl, buffer)) {
        CoTaskMemFree(pidl);
        return FALSE;
    }
    CoTaskMemFree(pidl);
    return TRUE;
}

static BOOL pick_open_file(HWND owner, const wchar_t *filter, const wchar_t *initial_dir, wchar_t *buffer, size_t buffer_count) {
    OPENFILENAMEW ofn = {0};
    buffer[0] = L'\0';
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (DWORD)buffer_count;
    ofn.lpstrInitialDir = initial_dir;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    return GetOpenFileNameW(&ofn);
}

static void set_default_bundle_name_from_path(HWND edit, const wchar_t *path) {
    const wchar_t *name = path;
    const wchar_t *dot;
    wchar_t buffer[MAX_PATH];
    wchar_t current[MAX_PATH];
    GetWindowTextW(edit, current, _countof(current));
    if (current[0] != L'\0') {
        return;
    }
    if (!path || path[0] == L'\0') {
        return;
    }
    if (wcsrchr(path, L'\\')) {
        name = wcsrchr(path, L'\\') + 1;
    }
    if (wcsrchr(name, L'/')) {
        name = wcsrchr(name, L'/') + 1;
    }
    wcsncpy(buffer, name, _countof(buffer) - 1);
    buffer[_countof(buffer) - 1] = L'\0';
    dot = wcsrchr(buffer, L'.');
    if (dot) {
        ((wchar_t *)dot)[0] = L'\0';
    }
    if (buffer[0] != L'\0') {
        SetWindowTextW(edit, buffer);
    }
}

static BOOL bundle_name_is_valid(const wchar_t *name) {
    static const wchar_t *invalid = L"<>:\"/\\|?*";
    return name[0] != L'\0' && wcspbrk(name, invalid) == NULL;
}

static BOOL bundle_name_conflicts_with_startup_exe(const wchar_t *bundle_name, const wchar_t *startup_path) {
    const wchar_t *startup_name = startup_path;
    wchar_t startup_base_name[MAX_PATH];
    wchar_t *dot;

    if (!startup_path || startup_path[0] == L'\0') {
        return FALSE;
    }
    if (wcsrchr(startup_name, L'\\')) {
        startup_name = wcsrchr(startup_name, L'\\') + 1;
    }
    if (wcsrchr(startup_name, L'/')) {
        startup_name = wcsrchr(startup_name, L'/') + 1;
    }
    if (_wcsicmp(bundle_name, startup_name) == 0) {
        return TRUE;
    }
    wcsncpy(startup_base_name, startup_name, _countof(startup_base_name) - 1);
    startup_base_name[_countof(startup_base_name) - 1] = L'\0';
    dot = wcsrchr(startup_base_name, L'.');
    if (dot && _wcsicmp(dot, L".exe") == 0) {
        *dot = L'\0';
    }
    return _wcsicmp(bundle_name, startup_base_name) == 0;
}

static BOOL bundle_name_conflicts_with_bundled_file(const wchar_t *bundle_name, const FileList *files) {
    wchar_t bundle_file_name[MAX_PATH];
    size_t i;

    _snwprintf(bundle_file_name, _countof(bundle_file_name), L"%ls.exe", bundle_name);
    bundle_file_name[_countof(bundle_file_name) - 1] = L'\0';

    for (i = 0; i < files->count; ++i) {
        wchar_t *relative_path = wide_from_utf8(files->items[i].relative_utf8);
        BOOL match;
        if (!relative_path) {
            continue;
        }
        match = _wcsicmp(relative_path, bundle_file_name) == 0;
        free(relative_path);
        if (match) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL get_state_file_path(wchar_t *buffer, size_t buffer_count) {
    if (!GetModuleFileNameW(NULL, buffer, (DWORD)buffer_count)) {
        return FALSE;
    }
    {
        wchar_t *dot = wcsrchr(buffer, L'.');
        if (dot) {
            wcscpy(dot, L".ini");
        } else {
            wcscat(buffer, L".ini");
        }
    }
    return TRUE;
}

static void set_default_output_folder(void) {
    wchar_t path[MAX_PATH * 4];
    wchar_t *slash;
    wchar_t output_path[MAX_PATH * 4];
    wchar_t current[MAX_PATH * 4];

    GetWindowTextW(g_ui.output_edit, current, _countof(current));
    if (current[0] != L'\0') {
        return;
    }
    if (!GetModuleFileNameW(NULL, path, (DWORD)_countof(path))) {
        return;
    }
    slash = wcsrchr(path, L'\\');
    if (!slash) {
        return;
    }
    *slash = L'\0';
    swprintf(output_path, _countof(output_path), L"%ls\\output", path);
    CreateDirectoryW(output_path, NULL);
    SetWindowTextW(g_ui.output_edit, output_path);
}

static void load_ui_state(void) {
    wchar_t path[MAX_PATH * 4];
    wchar_t value[MAX_PATH * 4];
    if (!get_state_file_path(path, _countof(path))) {
        return;
    }
    GetPrivateProfileStringW(L"builder", L"source", L"", value, (DWORD)_countof(value), path);
    SetWindowTextW(g_ui.source_edit, value);
    GetPrivateProfileStringW(L"builder", L"startup", L"", value, (DWORD)_countof(value), path);
    SetWindowTextW(g_ui.startup_edit, value);
    GetPrivateProfileStringW(L"builder", L"output_folder", L"", value, (DWORD)_countof(value), path);
    SetWindowTextW(g_ui.output_edit, value);
    set_default_output_folder();
    GetPrivateProfileStringW(L"builder", L"bundle_name", L"", value, (DWORD)_countof(value), path);
    SetWindowTextW(g_ui.output_name_edit, value);
    GetPrivateProfileStringW(L"builder", L"icon", L"", value, (DWORD)_countof(value), path);
    SetWindowTextW(g_ui.icon_edit, value);
    if (GetPrivateProfileIntW(L"builder", L"keep_files", 0, path)) {
        SendMessageW(g_ui.keep_files_check, BM_SETCHECK, BST_CHECKED, 0);
    } else {
        SendMessageW(g_ui.keep_files_check, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    if (GetPrivateProfileIntW(L"builder", L"extract_to_temp", 0, path)) {
        SendMessageW(g_ui.extract_to_temp_check, BM_SETCHECK, BST_CHECKED, 0);
    } else {
        SendMessageW(g_ui.extract_to_temp_check, BM_SETCHECK, BST_UNCHECKED, 0);
    }
    set_builder_compression_mode((uint32_t)GetPrivateProfileIntW(
        L"builder", L"compression_mode", BUILDER_COMPRESSION_XPRESS_HUFF, path));
}

static void save_ui_state(void) {
    wchar_t path[MAX_PATH * 4];
    wchar_t value[MAX_PATH * 4];
    wchar_t keep_files[8];
    wchar_t extract_to_temp[8];
    wchar_t compression_mode[8];
    if (!get_state_file_path(path, _countof(path))) {
        return;
    }
    GetWindowTextW(g_ui.source_edit, value, _countof(value));
    WritePrivateProfileStringW(L"builder", L"source", value, path);
    GetWindowTextW(g_ui.startup_edit, value, _countof(value));
    WritePrivateProfileStringW(L"builder", L"startup", value, path);
    GetWindowTextW(g_ui.output_edit, value, _countof(value));
    WritePrivateProfileStringW(L"builder", L"output_folder", value, path);
    GetWindowTextW(g_ui.output_name_edit, value, _countof(value));
    WritePrivateProfileStringW(L"builder", L"bundle_name", value, path);
    GetWindowTextW(g_ui.icon_edit, value, _countof(value));
    WritePrivateProfileStringW(L"builder", L"icon", value, path);
    swprintf(keep_files, _countof(keep_files), L"%d",
        SendMessageW(g_ui.keep_files_check, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
    WritePrivateProfileStringW(L"builder", L"keep_files", keep_files, path);
    swprintf(extract_to_temp, _countof(extract_to_temp), L"%d",
        SendMessageW(g_ui.extract_to_temp_check, BM_GETCHECK, 0, 0) == BST_CHECKED ? 1 : 0);
    WritePrivateProfileStringW(L"builder", L"extract_to_temp", extract_to_temp, path);
    swprintf(compression_mode, _countof(compression_mode), L"%u", (unsigned)get_builder_compression_mode());
    WritePrivateProfileStringW(L"builder", L"compression_mode", compression_mode, path);
}

static void pick_startup_exe(HWND owner) {
    wchar_t source[MAX_PATH * 4];
    wchar_t selected[MAX_PATH * 4];
    size_t source_len;
    GetWindowTextW(g_ui.source_edit, source, _countof(source));
    if (source[0] == L'\0') {
        MessageBoxW(owner, L"Pick the source folder first.", APP_TITLE, MB_ICONINFORMATION);
        return;
    }
    if (!pick_open_file(owner, L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0", source, selected, _countof(selected))) {
        return;
    }
    source_len = wcslen(source);
    if (!path_is_within_directory(selected, source)) {
        MessageBoxW(owner, L"Choose an EXE inside the source folder so the stored path stays relative.", APP_TITLE, MB_ICONWARNING);
        return;
    }
    if (selected[source_len] == L'\\' || selected[source_len] == L'/') {
        SetWindowTextW(g_ui.startup_edit, selected + source_len + 1);
    } else {
        SetWindowTextW(g_ui.startup_edit, selected + source_len);
    }
    set_default_bundle_name_from_path(g_ui.output_name_edit, selected);
}

static void create_child_controls(HWND window) {
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    CreateWindowW(L"STATIC", L"Source Folder", WS_CHILD | WS_VISIBLE, 16, 18, 120, 20, window, NULL, NULL, NULL);
    g_ui.source_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 40, 600, 24, window, (HMENU)IDC_SOURCE_EDIT, NULL, NULL);
    CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 626, 38, 110, 24, window, (HMENU)IDC_SOURCE_BROWSE, NULL, NULL);

    CreateWindowW(L"STATIC", L"Startup EXE Inside Folder (optional)", WS_CHILD | WS_VISIBLE, 16, 76, 250, 20, window, NULL, NULL, NULL);
    g_ui.startup_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 98, 600, 24, window, (HMENU)IDC_STARTUP_EDIT, NULL, NULL);
    CreateWindowW(L"BUTTON", L"Pick EXE...", WS_CHILD | WS_VISIBLE, 626, 96, 110, 24, window, (HMENU)IDC_STARTUP_BROWSE, NULL, NULL);

    CreateWindowW(L"STATIC", L"Output Folder", WS_CHILD | WS_VISIBLE, 16, 134, 180, 20, window, NULL, NULL, NULL);
    g_ui.output_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 156, 600, 24, window, (HMENU)IDC_OUTPUT_EDIT, NULL, NULL);
    CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 626, 154, 110, 24, window, (HMENU)IDC_OUTPUT_BROWSE, NULL, NULL);

    CreateWindowW(L"STATIC", L"Bundle Name", WS_CHILD | WS_VISIBLE, 16, 192, 180, 20, window, NULL, NULL, NULL);
    g_ui.output_name_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 214, 330, 24, window, (HMENU)IDC_OUTPUT_NAME_EDIT, NULL, NULL);
    CreateWindowW(L"STATIC", L".exe", WS_CHILD | WS_VISIBLE, 354, 216, 40, 20, window, NULL, NULL, NULL);

    g_ui.keep_files_check = CreateWindowW(L"BUTTON", L"Keep extracted files after run", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 248, 260, 22, window, (HMENU)IDC_KEEP_FILES_CHECK, NULL, NULL);
    g_ui.extract_to_temp_check = CreateWindowW(L"BUTTON", L"Extract to a temp folder and run from there", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, 274, 320, 22, window, (HMENU)IDC_EXTRACT_TO_TEMP_CHECK, NULL, NULL);

    CreateWindowW(L"STATIC", L"Compression Mode", WS_CHILD | WS_VISIBLE, 16, 308, 180, 20, window, NULL, NULL, NULL);
    g_ui.compression_store_radio = CreateWindowW(L"BUTTON", L"Store only", WS_CHILD | WS_VISIBLE | WS_GROUP | BS_AUTORADIOBUTTON, 16, 330, 120, 22, window, (HMENU)IDC_COMPRESSION_STORE_RADIO, NULL, NULL);
    g_ui.compression_xpress_radio = CreateWindowW(L"BUTTON", L"XPRESS", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 150, 330, 100, 22, window, (HMENU)IDC_COMPRESSION_XPRESS_RADIO, NULL, NULL);
    g_ui.compression_xpress_huff_radio = CreateWindowW(L"BUTTON", L"XPRESS_HUFF", WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON, 264, 330, 150, 22, window, (HMENU)IDC_COMPRESSION_XPRESS_HUFF_RADIO, NULL, NULL);

    CreateWindowW(L"STATIC", L"Icon Source (.ico or .exe, optional)", WS_CHILD | WS_VISIBLE, 16, 364, 250, 20, window, NULL, NULL, NULL);
    g_ui.icon_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 16, 386, 600, 24, window, (HMENU)IDC_ICON_EDIT, NULL, NULL);
    CreateWindowW(L"BUTTON", L"Browse...", WS_CHILD | WS_VISIBLE, 626, 384, 110, 24, window, (HMENU)IDC_ICON_BROWSE, NULL, NULL);

    g_ui.build_button = CreateWindowW(L"BUTTON", L"Build Bundle", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 16, 422, 140, 30, window, (HMENU)IDC_BUILD_BUTTON, NULL, NULL);
    g_ui.status_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, 16, 468, 720, 90, window, (HMENU)IDC_STATUS_EDIT, NULL, NULL);

    SendMessageW(g_ui.source_edit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.startup_edit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.output_edit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.output_name_edit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.keep_files_check, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.extract_to_temp_check, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.compression_store_radio, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.compression_xpress_radio, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.compression_xpress_huff_radio, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.icon_edit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.status_edit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(g_ui.build_button, WM_SETFONT, (WPARAM)font, TRUE);
    load_ui_state();
}

static LRESULT CALLBACK main_wnd_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE:
            create_child_controls(window);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case IDC_SOURCE_BROWSE: {
                    wchar_t folder[MAX_PATH * 4];
                    if (pick_folder(window, folder)) {
                        SetWindowTextW(g_ui.source_edit, folder);
                        set_default_bundle_name_from_path(g_ui.output_name_edit, folder);
                    }
                    return 0;
                }
                case IDC_STARTUP_BROWSE:
                    pick_startup_exe(window);
                    return 0;
                case IDC_OUTPUT_BROWSE: {
                    wchar_t folder[MAX_PATH * 4];
                    if (pick_folder(window, folder)) {
                        SetWindowTextW(g_ui.output_edit, folder);
                    }
                    return 0;
                }
                case IDC_ICON_BROWSE: {
                    wchar_t path[MAX_PATH * 4];
                    if (pick_open_file(window, L"Icon Sources (*.ico;*.exe)\0*.ico;*.exe\0Icon Files (*.ico)\0*.ico\0Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0", NULL, path, _countof(path))) {
                        SetWindowTextW(g_ui.icon_edit, path);
                    }
                    return 0;
                }
                case IDC_BUILD_BUTTON:
                    EnableWindow(g_ui.build_button, FALSE);
                    save_ui_state();
                    build_bundle(window);
                    EnableWindow(g_ui.build_button, TRUE);
                    return 0;
            }
            break;
        case WM_DESTROY:
            save_ui_state();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static int run_builder_gui(HINSTANCE instance) {
    const wchar_t class_name[] = L"BundlerWindowClass";
    WNDCLASSEXW wc = {0};
    MSG msg;
    HWND window;
    // Load the embedded icon resource from the executable and use it for
    // both the large and small window class icons.
    HICON icon = LoadIconW(instance, MAKEINTRESOURCEW(1));

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = main_wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = icon;
    wc.hIconSm = icon;
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    RegisterClassExW(&wc);

    window = CreateWindowExW(0, class_name, APP_TITLE, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 770, 650, NULL, NULL, instance, NULL);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}

static int app_main(HINSTANCE instance) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (run_bundled_mode(instance)) {
        CoUninitialize();
        return 0;
    }
    {
        int code = run_builder_gui(instance);
        CoUninitialize();
        return code;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR command_line, int show_cmd) {
    (void)prev_instance;
    (void)command_line;
    (void)show_cmd;
    return app_main(instance);
}
