#ifndef FILE_SYSTEM_H
#define FILE_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Cross-platform file path separator
#ifdef _WIN32
#define PATH_SEPARATOR "\\"
#define PATH_SEPARATOR_CHAR '\\'
#else
#define PATH_SEPARATOR "/"
#define PATH_SEPARATOR_CHAR '/'
#endif

// File attributes
typedef enum {
    FILE_ATTR_READONLY = 1 << 0,
    FILE_ATTR_HIDDEN = 1 << 1,
    FILE_ATTR_SYSTEM = 1 << 2,
    FILE_ATTR_DIRECTORY = 1 << 3,
    FILE_ATTR_ARCHIVE = 1 << 4
} FileAttributes;

// File open modes
typedef enum {
    FILE_MODE_READ = 1 << 0,
    FILE_MODE_WRITE = 1 << 1,
    FILE_MODE_CREATE = 1 << 2,
    FILE_MODE_TRUNCATE = 1 << 3,
    FILE_MODE_APPEND = 1 << 4
} FileMode;

// Directory entry information
typedef struct {
    char name[256];
    uint64_t size;
    uint64_t modified_time;
    FileAttributes attributes;
} DirectoryEntry;

// File handle (opaque)
typedef struct FileHandle FileHandle;

// Directory handle (opaque)
typedef struct DirectoryHandle DirectoryHandle;

// File system interface
typedef struct {
    // File operations
    FileHandle* (*open_file)(const char* path, FileMode mode);
    void (*close_file)(FileHandle* handle);
    size_t (*read_file)(FileHandle* handle, void* buffer, size_t size);
    size_t (*write_file)(FileHandle* handle, const void* buffer, size_t size);
    bool (*seek_file)(FileHandle* handle, int64_t offset, int whence);
    uint64_t (*tell_file)(FileHandle* handle);
    uint64_t (*get_file_size)(FileHandle* handle);
    bool (*flush_file)(FileHandle* handle);

    // Directory operations
    DirectoryHandle* (*open_directory)(const char* path);
    void (*close_directory)(DirectoryHandle* handle);
    bool (*read_directory)(DirectoryHandle* handle, DirectoryEntry* entry);
    bool (*create_directory)(const char* path);
    bool (*remove_directory)(const char* path);

    // Path operations
    bool (*file_exists)(const char* path);
    bool (*directory_exists)(const char* path);
    bool (*delete_file)(const char* path);
    bool (*copy_file)(const char* src, const char* dst);
    bool (*move_file)(const char* src, const char* dst);
    bool (*get_file_info)(const char* path, DirectoryEntry* info);

    // Path utilities
    char* (*get_current_directory)(char* buffer, size_t size);
    bool (*set_current_directory)(const char* path);
    char* (*get_absolute_path)(const char* path, char* buffer, size_t size);
    bool (*is_absolute_path)(const char* path);
    void (*normalize_path)(char* path);
    void (*join_paths)(char* result, size_t result_size, const char* path1, const char* path2);

    // Special directories
    char* (*get_user_home_directory)(char* buffer, size_t size);
    char* (*get_app_data_directory)(char* buffer, size_t size);
    char* (*get_temp_directory)(char* buffer, size_t size);
    char* (*get_executable_directory)(char* buffer, size_t size);
} FileSystem;

// Get platform-specific file system implementation
FileSystem* get_file_system(void);

// Utility functions
const char* get_file_extension(const char* path);
void remove_file_extension(char* path);
const char* get_filename_from_path(const char* path);
void get_directory_from_path(const char* path, char* buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif // FILE_SYSTEM_H
