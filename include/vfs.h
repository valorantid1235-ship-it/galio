#ifndef VFS_H
#define VFS_H

#include "common.h"
#include <stddef.h>

/* Virtual Filesystem Layer - Linux-like with directory support */

#define VFS_MAGIC 0xDEADBEEF
#define VFS_VERSION 1
#define VFS_MAX_FILES 512
#define VFS_MAX_PATH 512
#define VFS_MAX_FILENAME 256

typedef struct {
    char path[VFS_MAX_PATH];
    u32 size;
    u32 offset;
    u32 is_dir;
    u32 permissions;
} vfs_entry_t;

typedef struct {
    u32 magic;
    u32 version;
    u32 entry_count;
    u32 data_offset;
    vfs_entry_t entries[VFS_MAX_FILES];
} vfs_header_t;

typedef struct {
    char filename[VFS_MAX_FILENAME];
    u32 size;
    u32 is_dir;
} vfs_dirent_t;

/* Initialize VFS with initrd */
void vfs_init(void *initrd_addr);

/* Find entry by path (file or directory) */
vfs_entry_t *vfs_find(const char *path);

/* Read file by path */
u32 vfs_read(const char *path, void *buffer, u32 size);

/* List directory */
void vfs_listdir(const char *path);

/* List all files in filesystem */
void vfs_listall(void);

/* Get entry info */
u32 vfs_size(const char *path);
u32 vfs_is_dir(const char *path);
const char *vfs_basename(const char *path);

/* Statistics */
void vfs_stats(void);
void vfs_debug(void);

/* Advanced operations */
void vfs_tree(void);
u32 vfs_count_files(const char *path);
u32 vfs_count_dirs(const char *path);
u32 vfs_dir_size(const char *path);

/* Directory creation/deletion */
u32 vfs_mkdir(const char *path);
u32 vfs_rmdir(const char *path);

#endif /* VFS_H */
