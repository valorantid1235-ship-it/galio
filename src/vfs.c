/* vfs.c - Virtual Filesystem Layer with Linux-like paths */
#include "vfs.h"
#include "kprintf.h"
#include "string.h"

static vfs_header_t *vfs_root = NULL;
static u32 total_files = 0;
static u32 total_size = 0;

void vfs_init(void *initrd_addr) {
    vfs_root = (vfs_header_t *)initrd_addr;

    if (!vfs_root) {
        kprintf("[VFS] ERROR: No initrd mounted\n");
        return;
    }

    if (vfs_root->magic != VFS_MAGIC) {
        kprintf("[VFS] ERROR: Invalid magic number %08X (expected %08X)\n",
                vfs_root->magic, VFS_MAGIC);
        vfs_root = NULL;
        return;
    }

    total_files = 0;
    total_size = 0;
    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        if (vfs_root->entries[i].size > 0) {
            total_files++;
            total_size += vfs_root->entries[i].size;
        }
    }

    kprintf("[VFS] ✓ Filesystem mounted successfully\n");
    kprintf("[VFS] - Magic: 0x%08X\n", vfs_root->magic);
    kprintf("[VFS] - Version: %u\n", vfs_root->version);
    kprintf("[VFS] - Total entries: %u\n", vfs_root->entry_count);
    kprintf("[VFS] - Data files: %u\n", total_files);
    kprintf("[VFS] - Data size: %u bytes\n", total_size);
}

static char *path_normalize(const char *path) {
    static char buf[VFS_MAX_PATH];
    int i = 0, j = 0;

    if (!path) {
        buf[0] = '/';
        buf[1] = 0;
        return buf;
    }

    while (path[i] && j < VFS_MAX_PATH - 1) {
        if (path[i] == '/' && i > 0 && path[i-1] == '/') {
            i++;
            continue;
        }
        buf[j++] = path[i++];
    }
    buf[j] = 0;

    if (j > 1 && buf[j-1] == '/') {
        buf[j-1] = 0;
    }

    return buf;
}

vfs_entry_t *vfs_find(const char *path) {
    if (!vfs_root || !path) return NULL;

    char *norm_path = path_normalize(path);

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        if (__builtin_strcmp(vfs_root->entries[i].path, norm_path) == 0) {
            return &vfs_root->entries[i];
        }
    }

    return NULL;
}

u32 vfs_read(const char *path, void *buffer, u32 size) {
    vfs_entry_t *entry = vfs_find(path);
    if (!entry) {
        kprintf("[VFS] ERROR: File not found: %s\n", path);
        return 0;
    }

    if (entry->is_dir) {
        kprintf("[VFS] ERROR: Cannot read directory: %s\n", path);
        return 0;
    }

    u32 to_read = (size < entry->size) ? size : entry->size;
    u8 *src = (u8 *)vfs_root + entry->offset;
    u8 *dst = (u8 *)buffer;

    for (u32 i = 0; i < to_read; i++) {
        dst[i] = src[i];
    }

    return to_read;
}

static char *get_parent_dir(const char *path) {
    static char buf[VFS_MAX_PATH];
    const char *src = path;
    char *dst = buf;
    while (*src) *dst++ = *src++;
    *dst = 0;

    int i = __builtin_strlen(buf) - 1;
    while (i > 0 && buf[i] != '/') i--;

    if (i == 0) {
        buf[1] = 0;
    } else {
        buf[i] = 0;
    }

    return buf;
}

static u32 path_depth(const char *path) {
    u32 depth = 0;
    for (u32 i = 0; path[i]; i++) {
        if (path[i] == '/') depth++;
    }
    return depth;
}

const char *vfs_basename(const char *path) {
    static char buf[VFS_MAX_FILENAME];
    const char *src = path;
    int i = __builtin_strlen(path) - 1;

    while (i > 0 && path[i] != '/') i--;
    if (path[i] == '/') i++;

    int j = 0;
    while (path[i] && j < VFS_MAX_FILENAME - 1) {
        buf[j++] = path[i++];
    }
    buf[j] = 0;
    return buf;
}

void vfs_listdir(const char *path) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return;
    }

    char *norm_path = path_normalize(path);
    vfs_entry_t *dir_entry = vfs_find(norm_path);

    if (!dir_entry) {
        kprintf("[VFS] ERROR: Directory not found: %s\n", path);
        return;
    }

    if (!dir_entry->is_dir) {
        kprintf("[VFS] ERROR: Not a directory: %s\n", path);
        return;
    }

    kprintf("Directory listing: %s\n", norm_path);

    u32 count = 0;
    u32 dir_count = 0;
    u32 file_count = 0;
    u32 total_dir_size = 0;

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->path[0] == 0) continue;

        char *parent = get_parent_dir(e->path);
        if (__builtin_strcmp(parent, norm_path) == 0) {
            if (e->is_dir) {
                kprintf("    [DIR]  %s/\n", vfs_basename(e->path));
                dir_count++;
            } else {
                kprintf("    [FILE] %s (%u bytes)\n", vfs_basename(e->path), e->size);
                file_count++;
                total_dir_size += e->size;
            }
            count++;
        }
    }

    kprintf("    ─────────────────────────────────────────────\n");
    kprintf("    Dirs: %u | Files: %u | Total size: %u bytes\n",
            dir_count, file_count, total_dir_size);
}

void vfs_listall(void) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return;
    }

    kprintf("[VFS] Complete filesystem tree:\n");
    kprintf("================================================================\n");

    u32 depth_stack[32];
    u32 last_depth = 0;

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->path[0] == 0) continue;

        u32 depth = path_depth(e->path) - 1;

        for (u32 d = 0; d < depth; d++) {
            kprintf("  ");
        }

        if (e->is_dir) {
            kprintf("├─ [DIR]  %s/\n", vfs_basename(e->path));
        } else {
            kprintf("├─ [FILE] %-24s %8u B\n", vfs_basename(e->path), e->size);
        }
    }
    kprintf("================================================================\n");
}

u32 vfs_size(const char *path) {
    vfs_entry_t *entry = vfs_find(path);
    return entry ? entry->size : 0;
}

u32 vfs_is_dir(const char *path) {
    vfs_entry_t *entry = vfs_find(path);
    return entry ? entry->is_dir : 0;
}

void vfs_stats(void) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return;
    }

    u32 files = 0, dirs = 0;
    u32 total_data_size = 0;

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->path[0] == 0) continue;
        if (e->is_dir) {
            dirs++;
        } else {
            files++;
            total_data_size += e->size;
        }
    }

    kprintf("[VFS] Filesystem Statistics:\n");
    kprintf("    Total entries:    %u\n", vfs_root->entry_count);
    kprintf("    Directories:      %u\n", dirs);
    kprintf("    Files:            %u\n", files);
    kprintf("    Total data size:  %u bytes (%.2f KB)\n",
            total_data_size, (float)total_data_size / 1024.0);
    kprintf("    Header magic:     0x%08X\n", vfs_root->magic);
    kprintf("    Header version:   %u\n", vfs_root->version);
}

void vfs_debug(void) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return;
    }

    u32 dirs = 0, files = 0;
    u32 total_data_size = 0;
    u32 image_size = vfs_root->data_offset;

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->is_dir) {
            dirs++;
        } else {
            files++;
            total_data_size += e->size;
            image_size += e->size;
        }
    }

    kprintf("╔════════════════════════════════════════════════════════════════╗\n");
    kprintf("║           FILESYSTEM IMAGE DEBUG DUMP                            ║\n");
    kprintf("╠════════════════════════════════════════════════════════════════╣\n");
    kprintf("║ Generated: initrd.bin\n");
    kprintf("║ Header size: %u bytes\n", (u32)sizeof(vfs_header_t));
    kprintf("║ Total entries: %u\n", vfs_root->entry_count);
    kprintf("║ Data offset: 0x%X\n", vfs_root->data_offset);
    kprintf("║ Image size: %u bytes\n", image_size);
    kprintf("╠════════════════════════════════════════════════════════════════╣\n");
    kprintf("║ Filesystem Contents:\n");
    kprintf("║   Directories: %u\n", dirs);
    kprintf("║   Data files: %u\n", files);
    kprintf("║   Total data: %u bytes\n", total_data_size);
    kprintf("╠════════════════════════════════════════════════════════════════╣\n");
    kprintf("║ Directory Tree:\n");
    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->is_dir) {
            kprintf("║   [DIR]  %s/\n", e->path);
        }
    }
    kprintf("╠════════════════════════════════════════════════════════════════╣\n");
    kprintf("║ File List:\n");
    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (!e->is_dir) {
            kprintf("║   [FILE] %-40s %8u bytes\n", e->path, e->size);
        }
    }
    kprintf("╚════════════════════════════════════════════════════════════════╝\n");
}

u32 vfs_count_files(const char *path) {
    if (!vfs_root) return 0;

    char *norm_path = path_normalize(path);
    u32 count = 0;

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->path[0] == 0 || e->is_dir) continue;
        char *parent = get_parent_dir(e->path);
        if (__builtin_strcmp(parent, norm_path) == 0) {
            count++;
        }
    }
    return count;
}

u32 vfs_count_dirs(const char *path) {
    if (!vfs_root) return 0;

    char *norm_path = path_normalize(path);
    u32 count = 0;

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->path[0] == 0 || !e->is_dir) continue;
        char *parent = get_parent_dir(e->path);
        if (__builtin_strcmp(parent, norm_path) == 0) {
            count++;
        }
    }
    return count;
}

u32 vfs_dir_size(const char *path) {
    if (!vfs_root) return 0;

    char *norm_path = path_normalize(path);
    u32 size = 0;

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->path[0] == 0 || e->is_dir) continue;
        char *parent = get_parent_dir(e->path);
        if (__builtin_strcmp(parent, norm_path) == 0) {
            size += e->size;
        }
    }
    return size;
}

void vfs_tree(void) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return;
    }

    kprintf("[VFS] Filesystem tree with details:\n");
    kprintf("================================================================\n");

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e->path[0] == 0) continue;

        u32 depth = path_depth(e->path) - 1;

        for (u32 d = 0; d < depth; d++) {
            kprintf("  ");
        }

        if (e->is_dir) {
            u32 files = vfs_count_files(e->path);
            u32 dirs = vfs_count_dirs(e->path);
            u32 sz = vfs_dir_size(e->path);
            kprintf("├─ [DIR]  %-20s (%u files, %u dirs, %u bytes)\n",
                    vfs_basename(e->path), files, dirs, sz);
        } else {
            kprintf("├─ [FILE] %-20s %u bytes @ 0x%X\n",
                    vfs_basename(e->path), e->size, e->offset);
        }
    }
    kprintf("================================================================\n");
}

u32 vfs_mkdir(const char *path) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return 0;
    }

    char *norm_path = path_normalize(path);
    char norm_path_copy[VFS_MAX_PATH];
    int i = 0;
    while (norm_path[i] && i < VFS_MAX_PATH - 1) {
        norm_path_copy[i] = norm_path[i];
        i++;
    }
    norm_path_copy[i] = 0;

   // kprintf("[VFS_DEBUG] vfs_mkdir: input path=%s, normalized=%s\n", path, norm_path_copy);

    if (vfs_find(norm_path_copy)) {
        kprintf("Directory already exists: %s\n", norm_path_copy);
        return 0;
    }

    if (vfs_root->entry_count >= VFS_MAX_FILES) {
        kprintf("[VFS] ERROR: Filesystem full\n");
        return 0;
    }

    char *parent = get_parent_dir(norm_path_copy);
    char parent_copy[VFS_MAX_PATH];
    i = 0;
    while (parent[i] && i < VFS_MAX_PATH - 1) {
        parent_copy[i] = parent[i];
        i++;
    }
    parent_copy[i] = 0;

   // kprintf("[VFS_DEBUG] vfs_mkdir: parent=%s\n", parent_copy);
    if (__builtin_strcmp(parent_copy, "/") != 0 && !vfs_find(parent_copy)) {
        kprintf("[VFS] ERROR: Parent directory does not exist: %s\n", parent_copy);
        return 0;
    }

    u32 idx = vfs_root->entry_count;
    vfs_entry_t *new_entry = &vfs_root->entries[idx];

    i = 0;
    while (norm_path_copy[i] && i < VFS_MAX_PATH - 1) {
        new_entry->path[i] = norm_path_copy[i];
        i++;
    }
    new_entry->path[i] = 0;
    //kprintf("[VFS_DEBUG] vfs_mkdir: entry path set to=%s\n", new_entry->path);

    new_entry->size = 0;
    new_entry->offset = 0;
    new_entry->is_dir = 1;
    new_entry->permissions = 0755;

    vfs_root->entry_count++;

    kprintf("[VFS] Created directory: %s\n", norm_path_copy);
    return 1;
}

u32 vfs_create(const char *path, u8 force) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return 0;
    }

    char *norm_path = path_normalize(path);
    char norm_path_copy[VFS_MAX_PATH];
    int i = 0;
    while (norm_path[i] && i < VFS_MAX_PATH - 1) {
        norm_path_copy[i] = norm_path[i];
        i++;
    }
    norm_path_copy[i] = 0;

    vfs_entry_t *existing = vfs_find(norm_path_copy);
    if (existing) {
        if (existing->is_dir) {
            kprintf("[VFS] ERROR: Path is a directory, not a file: %s\n", norm_path_copy);
            return 0;
        }
        if (!force) {
            kprintf("[VFS] File already exists: %s\n", norm_path_copy);
            kprintf("[VFS] Use 'new file %s' to replace it.\n", norm_path_copy);
            return 0;
        }

        existing->size = 0;
        existing->offset = 0;
        existing->permissions = 0644;
        kprintf("[VFS] Replaced file: %s\n", norm_path_copy);
        return 1;
    }

    if (vfs_root->entry_count >= VFS_MAX_FILES) {
        kprintf("[VFS] ERROR: Filesystem full\n");
        return 0;
    }

    char *parent = get_parent_dir(norm_path_copy);
    if (!vfs_find(parent)) {
        kprintf("[VFS] ERROR: Parent directory does not exist: %s\n", parent);
        return 0;
    }

    u32 idx = vfs_root->entry_count;
    vfs_entry_t *new_entry = &vfs_root->entries[idx];
    i = 0;
    while (norm_path_copy[i] && i < VFS_MAX_PATH - 1) {
        new_entry->path[i] = norm_path_copy[i];
        i++;
    }
    new_entry->path[i] = 0;
    new_entry->size = 0;
    new_entry->offset = 0;
    new_entry->is_dir = 0;
    new_entry->permissions = 0644;
    vfs_root->entry_count++;

    kprintf("[VFS] Created file: %s\n", norm_path_copy);
    return 1;
}

u32 vfs_rmdir(const char *path) {
    if (!vfs_root) {
        kprintf("[VFS] ERROR: Filesystem not mounted\n");
        return 0;
    }

    char *norm_path = path_normalize(path);
    char norm_path_copy[VFS_MAX_PATH];
    int i = 0;
    while (norm_path[i] && i < VFS_MAX_PATH - 1) {
        norm_path_copy[i] = norm_path[i];
        i++;
    }
    norm_path_copy[i] = 0;

    vfs_entry_t *entry = vfs_find(norm_path_copy);

    if (!entry) {
        kprintf("[VFS] ERROR: Directory not found: %s\n", norm_path_copy);
        return 0;
    }

    if (!entry->is_dir) {
        kprintf("[VFS] ERROR: Not a directory: %s\n", norm_path_copy);
        return 0;
    }

    for (u32 i = 0; i < vfs_root->entry_count; i++) {
        vfs_entry_t *e = &vfs_root->entries[i];
        if (e == entry) continue;

        char *parent = get_parent_dir(e->path);
        if (__builtin_strcmp(parent, norm_path_copy) == 0) {
            kprintf("[VFS] ERROR: Directory not empty: %s\n", norm_path_copy);
            return 0;
        }
    }

    entry->size = 0;
    entry->is_dir = 0;
    kprintf("[VFS] Removed directory: %s\n", norm_path_copy);
    return 1;
}
