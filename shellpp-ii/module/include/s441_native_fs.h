#ifndef S441_NATIVE_FS_H
#define S441_NATIVE_FS_H

#include <stdint.h>

enum s441_fs_result {
    S441_FS_OK = 0,
    S441_FS_ERR_ARGUMENT = -200,
    S441_FS_ERR_PATH = -201,
    S441_FS_ERR_OPEN = -202,
    S441_FS_ERR_READ = -203,
    S441_FS_ERR_DIRECTORY = -204,
    S441_FS_ERR_CLOSE = -205,
    S441_FS_ERR_FORMAT = -206,
    S441_FS_ERR_TRUNCATED = -213,
};

#define S441_FS_PATH_CAP 192u
#define S441_FS_NAME_CAP 96u
/* One directory is rendered as one scrollable LVGL list. Keep a bounded
 * resident snapshot so callbacks never retain readdir-owned storage. */
#define S441_FS_DIR_PAGE_ENTRIES 64u

struct s441_fs_entry {
    char name[S441_FS_NAME_CAP];
    uint8_t type;
    uint8_t is_directory;
};

struct s441_fs_directory_page {
    struct s441_fs_entry entries[S441_FS_DIR_PAGE_ENTRIES];
    uint32_t start;
    uint8_t count;
    uint8_t has_previous;
    uint8_t has_next;
};

/* Reads a small, known text source into permanent caller storage. */
int s441_fs_read_text(const char *path, char *text, uint32_t capacity,
    uint32_t *text_length);

/* Read one display-sized text page without relying on an unverified lseek ABI. */
int s441_fs_read_text_page(const char *path, uint32_t offset, char *text,
    uint32_t capacity, uint32_t *text_length, uint8_t *has_next);

/* The directory surface is intentionally read-only. */
int s441_fs_list_directory_page(const char *path, uint32_t start,
    struct s441_fs_directory_page *page);
int s441_fs_join(const char *base, const char *name, char *output,
    uint32_t capacity);
int s441_fs_parent(const char *path, char *output, uint32_t capacity);
const char *s441_fs_basename(const char *path);

/* Reads and normalizes the two procfs sources used by the native status page. */
int s441_fs_read_cpu(char *text, uint32_t capacity, uint32_t *percent);
int s441_fs_read_memory(char *text, uint32_t capacity, uint32_t *percent);
int s441_fs_read_status(char *text, uint32_t capacity);

#endif
