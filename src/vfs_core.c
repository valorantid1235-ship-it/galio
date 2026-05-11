#include "vfs_core.h"
#include "heap.h"
#include "kprintf.h"
#include "string.h"

static vfs_inode_t vfs_inodes[VFS_MAX_INODES];
static vfs_dentry_t vfs_dentries[VFS_MAX_DENTRIES];
static vfs_file_t vfs_files[VFS_MAX_FILE_HANDLES];
static vfs_dentry_t *vfs_dentry_cache[VFS_MAX_DENTRY_CACHE];
static u8 *vfs_data_ram = NULL;
static u32 vfs_data_ram_size = 0;
static u32 vfs_data_top = 0;
static u32 vfs_next_inode = 0;
static u32 vfs_next_dentry = 0;
static vfs_dentry_t *vfs_root_dentry = NULL;
static vfs_inode_t *vfs_root_inode = NULL;

static void vfs_reset_ram(void) {
    if (vfs_data_ram) {
        kfree(vfs_data_ram);
        vfs_data_ram = NULL;
    }
    vfs_data_ram_size = 0;
    vfs_data_top = 0;
}

static void vfs_reset_cache(void) {
    for (u32 i = 0; i < VFS_MAX_DENTRY_CACHE; i++) {
        vfs_dentry_cache[i] = NULL;
    }
}

static void vfs_reset_state(void) {
    vfs_next_inode = 0;
    vfs_next_dentry = 0;
    vfs_root_dentry = NULL;
    vfs_root_inode = NULL;
    for (u32 i = 0; i < VFS_MAX_INODES; i++) {
        vfs_inodes[i].number = 0xFFFFFFFFu;
        vfs_inodes[i].dirent_count = 0;
        vfs_inodes[i].dirent_capacity = 0;
        vfs_inodes[i].dirents = NULL;
    }
    for (u32 i = 0; i < VFS_MAX_DENTRIES; i++) {
        vfs_dentries[i].parent = NULL;
        vfs_dentries[i].first_child = NULL;
        vfs_dentries[i].next_sibling = NULL;
        vfs_dentries[i].hash_next = NULL;
        vfs_dentries[i].inode = NULL;
        vfs_dentries[i].name[0] = 0;
    }
    for (u32 i = 0; i < VFS_MAX_FILE_HANDLES; i++) {
        vfs_files[i].inode = NULL;
        vfs_files[i].pos = 0;
        vfs_files[i].flags = 0;
        vfs_files[i].ref_count = 0;
    }
    vfs_reset_cache();
}

static u32 vfs_hash_dentry(vfs_dentry_t *parent, const char *name) {
    u32 hash = 5381;
    if (parent && parent->inode) {
        hash = hash * 33 + parent->inode->number;
    }
    for (const char *p = name; *p; p++) {
        hash = ((hash << 5) + hash) + (u8)*p;
    }
    return hash % VFS_MAX_DENTRY_CACHE;
}

static vfs_dentry_t *vfs_cache_lookup(vfs_dentry_t *parent, const char *name) {
    if (!name) return NULL;
    u32 idx = vfs_hash_dentry(parent, name);
    for (u32 probe = 0; probe < VFS_MAX_DENTRY_CACHE; probe++) {
        u32 slot = (idx + probe) % VFS_MAX_DENTRY_CACHE;
        vfs_dentry_t *entry = vfs_dentry_cache[slot];
        if (!entry) continue;
        if (entry->parent == parent && strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void vfs_cache_insert(vfs_dentry_t *entry) {
    if (!entry || !entry->name) return;
    u32 idx = vfs_hash_dentry(entry->parent, entry->name);
    for (u32 probe = 0; probe < VFS_MAX_DENTRY_CACHE; probe++) {
        u32 slot = (idx + probe) % VFS_MAX_DENTRY_CACHE;
        if (!vfs_dentry_cache[slot] || vfs_dentry_cache[slot] == entry) {
            vfs_dentry_cache[slot] = entry;
            return;
        }
    }
    /* Evict simple linear slot to preserve most recent entries */
    vfs_dentry_cache[idx] = entry;
}

static vfs_dentry_t *vfs_alloc_dentry(void) {
    if (vfs_next_dentry >= VFS_MAX_DENTRIES) {
        return NULL;
    }
    vfs_dentry_t *dentry = &vfs_dentries[vfs_next_dentry++];
    dentry->parent = NULL;
    dentry->first_child = NULL;
    dentry->next_sibling = NULL;
    dentry->hash_next = NULL;
    dentry->inode = NULL;
    dentry->name[0] = 0;
    return dentry;
}

static vfs_inode_t *vfs_alloc_inode(void) {
    if (vfs_next_inode >= VFS_MAX_INODES) {
        return NULL;
    }
    vfs_inode_t *inode = &vfs_inodes[vfs_next_inode];
    inode->number = vfs_next_inode;
    inode->mode = 0;
    inode->size = 0;
    inode->uid = 0;
    inode->gid = 0;
    inode->atime = 0;
    inode->mtime = 0;
    inode->ctime = 0;
    inode->link_count = 1;
    inode->block_count = 0;
    inode->dirent_count = 0;
    inode->dirent_capacity = 0;
    inode->dirents = NULL;
    vfs_next_inode++;
    return inode;
}

static u32 vfs_allocate_data(u32 bytes) {
    if (!bytes) return 0;
    if (!vfs_data_ram) return 0xFFFFFFFFu;
    if (vfs_data_top + bytes > vfs_data_ram_size) {
        return 0xFFFFFFFFu;
    }
    u32 offset = vfs_data_top;
    vfs_data_top += bytes;
    return offset;
}

static u8 vfs_reserve_dirents(vfs_inode_t *inode, u32 required) {
    if (!inode || required <= inode->dirent_capacity) return 1;
    u32 new_capacity = inode->dirent_capacity ? inode->dirent_capacity * 2 : 8;
    while (new_capacity < required) {
        new_capacity *= 2;
    }
    vfs_core_dirent_t *new_block = krealloc(inode->dirents, new_capacity * sizeof(vfs_core_dirent_t));
    if (!new_block) return 0;
    inode->dirents = new_block;
    inode->dirent_capacity = new_capacity;
    return 1;
}

static vfs_core_dirent_t *vfs_find_dirent(vfs_inode_t *dir, const char *name) {
    if (!dir || !dir->dirents) return NULL;
    for (u32 i = 0; i < dir->dirent_count; i++) {
        if (strcmp(dir->dirents[i].name, name) == 0) {
            return &dir->dirents[i];
        }
    }
    return NULL;
}

static vfs_inode_t *vfs_find_inode_in_dir(vfs_inode_t *dir, const char *name) {
    vfs_core_dirent_t *dent = vfs_find_dirent(dir, name);
    if (!dent) return NULL;
    if (dent->inode_number >= VFS_MAX_INODES) return NULL;
    return &vfs_inodes[dent->inode_number];
}

static u8 vfs_dirent_add(vfs_inode_t *dir, const char *name, u32 inode_number) {
    if (!dir || !name) return 0;
    if (!vfs_reserve_dirents(dir, dir->dirent_count + 1)) return 0;
    vfs_core_dirent_t *dent = &dir->dirents[dir->dirent_count++];
    dent->inode_number = inode_number;
    strncpy(dent->name, name, VFS_MAX_FILENAME - 1);
    dent->name[VFS_MAX_FILENAME - 1] = 0;
    dir->size = dir->dirent_count * sizeof(vfs_core_dirent_t);
    return 1;
}

static u8 vfs_dirent_remove(vfs_inode_t *dir, const char *name) {
    if (!dir || !name || !dir->dirents) return 0;
    for (u32 i = 0; i < dir->dirent_count; i++) {
        if (strcmp(dir->dirents[i].name, name) == 0) {
            for (u32 j = i + 1; j < dir->dirent_count; j++) {
                dir->dirents[j - 1] = dir->dirents[j];
            }
            dir->dirent_count--;
            dir->size = dir->dirent_count * sizeof(vfs_core_dirent_t);
            return 1;
        }
    }
    return 0;
}

static void vfs_link_child(vfs_dentry_t *parent, vfs_dentry_t *child) {
    if (!parent || !child) return;
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
    vfs_cache_insert(child);
}

static vfs_dentry_t *vfs_create_dentry(vfs_dentry_t *parent, const char *name, vfs_inode_t *inode) {
    vfs_dentry_t *dentry = vfs_alloc_dentry();
    if (!dentry) return NULL;
    dentry->inode = inode;
    if (name) {
        strncpy(dentry->name, name, VFS_MAX_FILENAME - 1);
        dentry->name[VFS_MAX_FILENAME - 1] = 0;
    }
    if (!parent) {
        dentry->parent = dentry;
    } else {
        vfs_link_child(parent, dentry);
    }
    vfs_cache_insert(dentry);
    return dentry;
}

static void vfs_build_path_from_dentry(vfs_dentry_t *dentry, char *buffer) {
    if (!dentry || !buffer) return;
    if (dentry == vfs_root_dentry) {
        buffer[0] = '/';
        buffer[1] = 0;
        return;
    }
    char temp[VFS_MAX_PATH];
    temp[0] = 0;
    vfs_dentry_t *walker = dentry;
    while (walker && walker != vfs_root_dentry) {
        char component[VFS_MAX_FILENAME];
        strncpy(component, walker->name, VFS_MAX_FILENAME - 1);
        component[VFS_MAX_FILENAME - 1] = 0;
        char next[VFS_MAX_PATH];
        next[0] = 0;
        strncat(next, "/", VFS_MAX_PATH - 1);
        strncat(next, component, VFS_MAX_PATH - strlen(next) - 1);
        strncat(next, temp, VFS_MAX_PATH - strlen(next) - 1);
        strncpy(temp, next, VFS_MAX_PATH - 1);
        temp[VFS_MAX_PATH - 1] = 0;
        walker = walker->parent;
    }
    if (temp[0] == 0) {
        buffer[0] = '/';
        buffer[1] = 0;
    } else {
        strncpy(buffer, temp, VFS_MAX_PATH - 1);
        buffer[VFS_MAX_PATH - 1] = 0;
    }
}

static char *vfs_normalize(const char *path, char *buffer) {
    if (!path || !buffer) return NULL;
    u32 di = 0;
    u32 i = 0;
    while (path[i] && di + 1 < VFS_MAX_PATH) {
        if (path[i] == '/' && i > 0 && path[i - 1] == '/') {
            i++;
            continue;
        }
        buffer[di++] = path[i++];
    }
    buffer[di] = 0;
    if (di > 1 && buffer[di - 1] == '/') {
        buffer[--di] = 0;
    }
    if (di == 0) {
        buffer[0] = '/';
        buffer[1] = 0;
    }
    return buffer;
}

static const char *vfs_basename_for_path(const char *path, char *dest) {
    if (!path || !dest) return NULL;
    const char *end = path + strlen(path);
    while (end > path && *end != '/') {
        end--;
    }
    if (*end == '/') end++;
    strncpy(dest, end, VFS_MAX_FILENAME - 1);
    dest[VFS_MAX_FILENAME - 1] = 0;
    return dest;
}

static void vfs_split_parent(const char *path, char *parent, char *name) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize(path, normalized);
    if (strcmp(normalized, "/") == 0) {
        strncpy(parent, "/", VFS_MAX_PATH - 1);
        parent[VFS_MAX_PATH - 1] = 0;
        name[0] = 0;
        return;
    }
    const char *slash = NULL;
    for (const char *p = normalized; *p; p++) {
        if (*p == '/') slash = p;
    }
    if (!slash || slash == normalized) {
        strncpy(parent, "/", VFS_MAX_PATH - 1);
        parent[VFS_MAX_PATH - 1] = 0;
        strncpy(name, normalized + 1, VFS_MAX_FILENAME - 1);
        name[VFS_MAX_FILENAME - 1] = 0;
        return;
    }
    u32 parent_len = slash - normalized;
    if (parent_len >= VFS_MAX_PATH) parent_len = VFS_MAX_PATH - 1;
    memcpy(parent, normalized, parent_len);
    parent[parent_len] = 0;
    strncpy(name, slash + 1, VFS_MAX_FILENAME - 1);
    name[VFS_MAX_FILENAME - 1] = 0;
}

static vfs_dentry_t *vfs_lookup_internal(const char *path) {
    char normalized[VFS_MAX_PATH];
    vfs_normalize(path, normalized);
    if (strcmp(normalized, "/") == 0) {
        return vfs_root_dentry;
    }
    vfs_dentry_t *current = vfs_root_dentry;
    const char *cursor = normalized + 1;
    while (*cursor) {
        const char *next = cursor;
        while (*next && *next != '/') next++;
        char component[VFS_MAX_FILENAME];
        u32 length = next - cursor;
        if (length >= VFS_MAX_FILENAME) length = VFS_MAX_FILENAME - 1;
        memcpy(component, cursor, length);
        component[length] = 0;
        if (strcmp(component, ".") == 0) {
            /* remain */
        } else if (strcmp(component, "..") == 0) {
            if (current->parent) current = current->parent;
        } else {
            if (!current->inode || current->inode->mode != VFS_TYPE_DIR) {
                return NULL;
            }
            vfs_dentry_t *child = vfs_cache_lookup(current, component);
            if (!child) {
                vfs_inode_t *child_inode = vfs_find_inode_in_dir(current->inode, component);
                if (!child_inode) return NULL;
                child = vfs_create_dentry(current, component, child_inode);
                if (!child) return NULL;
            }
            current = child;
        }
        if (*next == 0) break;
        cursor = next + 1;
    }
    return current;
}

static vfs_dentry_t *vfs_lookup_parent_internal(const char *path) {
    char parent[VFS_MAX_PATH];
    char name[VFS_MAX_FILENAME];
    vfs_split_parent(path, parent, name);
    return vfs_lookup_internal(parent);
}

static vfs_dentry_t *vfs_make_directory_internal(const char *path, u8 force) {
    if (!path) return NULL;
    char normalized[VFS_MAX_PATH];
    vfs_normalize(path, normalized);
    if (strcmp(normalized, "/") == 0) return vfs_root_dentry;
    char parent_path[VFS_MAX_PATH];
    char name[VFS_MAX_FILENAME];
    vfs_split_parent(normalized, parent_path, name);
    vfs_dentry_t *parent = vfs_lookup_internal(parent_path);
    if (!parent || !parent->inode || parent->inode->mode != VFS_TYPE_DIR) {
        return NULL;
    }
    if (vfs_find_dirent(parent->inode, name)) {
        vfs_inode_t *existing = vfs_find_inode_in_dir(parent->inode, name);
        if (!existing) return NULL;
        if (existing->mode == VFS_TYPE_DIR) {
            return parent; /* already exists */
        }
        return NULL;
    }
    vfs_inode_t *inode = vfs_alloc_inode();
    if (!inode) return NULL;
    inode->mode = VFS_TYPE_DIR;
    inode->size = 0;
    inode->link_count = 1;
    inode->dirent_count = 0;
    inode->dirent_capacity = 0;
    inode->dirents = NULL;
    vfs_dirent_add(inode, ".", inode->number);
    vfs_dirent_add(inode, "..", parent->inode->number);
    if (!vfs_dirent_add(parent->inode, name, inode->number)) return NULL;
    vfs_dentry_t *child = vfs_create_dentry(parent, name, inode);
    return child;
}

static vfs_dentry_t *vfs_make_file_internal(const char *path, u8 force, const u8 *data, u32 size) {
    if (!path) return NULL;
    char normalized[VFS_MAX_PATH];
    vfs_normalize(path, normalized);
    char parent_path[VFS_MAX_PATH];
    char name[VFS_MAX_FILENAME];
    vfs_split_parent(normalized, parent_path, name);
    vfs_dentry_t *parent = vfs_lookup_internal(parent_path);
    if (!parent || !parent->inode || parent->inode->mode != VFS_TYPE_DIR) {
        return NULL;
    }
    vfs_inode_t *existing = vfs_find_inode_in_dir(parent->inode, name);
    if (existing) {
        if (existing->mode == VFS_TYPE_DIR) return NULL;
        if (!force) return NULL;
        u32 new_offset = vfs_allocate_data(size);
        if (new_offset == 0xFFFFFFFFu) return NULL;
        if (data && size) {
            memcpy(vfs_data_ram + new_offset, data, size);
        }
        existing->blocks[0] = new_offset;
        existing->block_count = 1;
        existing->size = size;
        return vfs_cache_lookup(parent, name);
    }
    vfs_inode_t *inode = vfs_alloc_inode();
    if (!inode) return NULL;
    inode->mode = VFS_TYPE_FILE;
    inode->size = size;
    inode->link_count = 1;
    u32 data_offset = 0;
    if (size > 0) {
        data_offset = vfs_allocate_data(size);
        if (data_offset == 0xFFFFFFFFu) return NULL;
        if (data) {
            memcpy(vfs_data_ram + data_offset, data, size);
        } else {
            memset(vfs_data_ram + data_offset, 0, size);
        }
        inode->blocks[0] = data_offset;
        inode->block_count = 1;
    }
    if (!vfs_dirent_add(parent->inode, name, inode->number)) return NULL;
    vfs_dentry_t *child = vfs_create_dentry(parent, name, inode);
    return child;
}

static void vfs_init_root(void) {
    vfs_root_inode = vfs_alloc_inode();
    if (!vfs_root_inode) return;
    vfs_root_inode->mode = VFS_TYPE_DIR;
    vfs_root_inode->size = 0;
    vfs_root_inode->link_count = 1;
    vfs_root_inode->dirent_count = 0;
    vfs_root_inode->dirent_capacity = 0;
    vfs_root_inode->dirents = NULL;
    vfs_root_dentry = vfs_create_dentry(NULL, "", vfs_root_inode);
    if (vfs_root_dentry) {
        vfs_root_dentry->parent = vfs_root_dentry;
    }
    vfs_dirent_add(vfs_root_inode, ".", vfs_root_inode->number);
    vfs_dirent_add(vfs_root_inode, "..", vfs_root_inode->number);
}

static void vfs_build_from_initrd(vfs_header_t *header) {
    if (!header) return;
    u32 total_data = 0;
    for (u32 i = 0; i < header->entry_count; i++) {
        if (!header->entries[i].is_dir) {
            total_data += header->entries[i].size;
        }
    }
    vfs_data_ram_size = total_data + VFS_RAMDISK_EXTRA;
    if (vfs_data_ram_size < header->data_offset + total_data) {
        vfs_data_ram_size = header->data_offset + total_data + VFS_RAMDISK_EXTRA;
    }
    vfs_data_ram = kmalloc(vfs_data_ram_size);
    if (!vfs_data_ram) {
        kprintf("[VFS] ERROR: Unable to allocate RAM disk buffer\n");
        return;
    }
    vfs_data_top = 0;
    for (u32 i = 0; i < header->entry_count; i++) {
        vfs_entry_t *entry = &header->entries[i];
        if (!entry->path[0]) continue;
        if (entry->is_dir) {
            vfs_make_directory_internal(entry->path, 1);
            continue;
        }
        char parent[VFS_MAX_PATH];
        char name[VFS_MAX_FILENAME];
        vfs_split_parent(entry->path, parent, name);
        if (parent[0] != 0 && strcmp(parent, "/") != 0) {
            vfs_make_directory_internal(parent, 1);
        }
        u32 size = entry->size;
        u8 *source = (u8 *)header + entry->offset;
        vfs_make_file_internal(entry->path, 1, source, size);
    }
}

void vfs_core_init(void *initrd_addr) {
    if (!initrd_addr) {
        kprintf("[VFS] ERROR: No initrd address supplied\n");
        return;
    }
    vfs_header_t *header = (vfs_header_t *)initrd_addr;
    if (header->magic != VFS_MAGIC) {
        kprintf("[VFS] ERROR: Invalid initrd magic %08X\n", header->magic);
        return;
    }
    vfs_reset_state();
    vfs_init_root();
    if (!vfs_root_dentry) {
        kprintf("[VFS] ERROR: Root directory initialization failed\n");
        return;
    }
    vfs_build_from_initrd(header);
    kprintf("[VFS] Core filesystem initialized in RAM\n");
}

vfs_dentry_t *vfs_core_lookup(const char *path, u32 flags) {
    if (!path) return NULL;
    if (flags & VFS_LOOKUP_PARENT) {
        return vfs_lookup_parent_internal(path);
    }
    return vfs_lookup_internal(path);
}

vfs_dentry_t *vfs_core_root(void) {
    return vfs_root_dentry;
}

void vfs_core_build_path(vfs_dentry_t *dentry, char *buffer) {
    vfs_build_path_from_dentry(dentry, buffer);
}

u32 vfs_core_open(const char *path) {
    vfs_dentry_t *dentry = vfs_core_lookup(path, 0);
    if (!dentry || !dentry->inode || dentry->inode->mode != VFS_TYPE_FILE) {
        return VFS_INVALID_FD;
    }
    for (u32 i = 0; i < VFS_MAX_FILE_HANDLES; i++) {
        if (vfs_files[i].ref_count == 0) {
            vfs_files[i].inode = dentry->inode;
            vfs_files[i].pos = 0;
            vfs_files[i].flags = 0;
            vfs_files[i].ref_count = 1;
            return i;
        }
    }
    return VFS_INVALID_FD;
}

u32 vfs_core_close(u32 fd) {
    if (fd >= VFS_MAX_FILE_HANDLES) return 0;
    if (vfs_files[fd].ref_count == 0) return 0;
    vfs_files[fd].ref_count = 0;
    vfs_files[fd].inode = NULL;
    vfs_files[fd].pos = 0;
    vfs_files[fd].flags = 0;
    return 1;
}

u32 vfs_core_write(u32 fd, const void *buffer, u32 size) {
    if (fd >= VFS_MAX_FILE_HANDLES) return 0;
    vfs_file_t *fh = &vfs_files[fd];
    if (!fh->inode || fh->ref_count == 0 || fh->inode->mode != VFS_TYPE_FILE) {
        return 0;
    }
    if (size > 0) {
        u32 offset = vfs_allocate_data(size);
        if (offset == 0xFFFFFFFFu) return 0;
        memcpy(vfs_data_ram + offset, buffer, size);
        fh->inode->blocks[0] = offset;
        fh->inode->block_count = 1;
    }
    fh->inode->size = size;
    fh->pos = size;
    return size;
}

u32 vfs_core_read_path(const char *path, void *buffer, u32 size) {
    if (!path || !buffer) return 0;
    vfs_dentry_t *dentry = vfs_core_lookup(path, 0);
    if (!dentry || !dentry->inode || dentry->inode->mode != VFS_TYPE_FILE) {
        return 0;
    }
    u32 to_read = size;
    if (to_read > dentry->inode->size) to_read = dentry->inode->size;
    if (to_read == 0) return 0;
    u32 offset = dentry->inode->blocks[0];
    if (offset == 0xFFFFFFFFu) return 0;
    memcpy(buffer, vfs_data_ram + offset, to_read);
    return to_read;
}

u32 vfs_core_create_file(const char *path, u8 force) {
    vfs_dentry_t *created = vfs_make_file_internal(path, force, NULL, 0);
    return created != NULL;
}

u32 vfs_core_create_dir(const char *path, u8 force) {
    vfs_dentry_t *created = vfs_make_directory_internal(path, force);
    return created != NULL;
}

vfs_inode_t *vfs_core_inode_by_number(u32 inode_number) {
    if (inode_number >= VFS_MAX_INODES) return NULL;
    return &vfs_inodes[inode_number];
}

u32 vfs_core_unlink(const char *path) {
    vfs_dentry_t *dentry = vfs_core_lookup(path, 0);
    if (!dentry || !dentry->inode || dentry->inode->mode != VFS_TYPE_FILE) {
        return 0;
    }
    if (dentry == vfs_root_dentry) return 0;
    vfs_dentry_t *parent = dentry->parent;
    if (!parent || !parent->inode) return 0;
    if (!vfs_dirent_remove(parent->inode, dentry->name)) return 0;
    dentry->inode->link_count--;
    if (dentry->inode->link_count == 0) {
        /* In practice we do not free data blocks immediately */
        dentry->inode->mode = 0;
    }
    return 1;
}

u32 vfs_core_rmdir(const char *path) {
    vfs_dentry_t *dentry = vfs_core_lookup(path, 0);
    if (!dentry || !dentry->inode || dentry->inode->mode != VFS_TYPE_DIR) {
        return 0;
    }
    if (dentry == vfs_root_dentry) return 0;
    u32 entries = 0;
    for (u32 i = 0; i < dentry->inode->dirent_count; i++) {
        const char *name = dentry->inode->dirents[i].name;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            entries++;
        }
    }
    if (entries > 0) {
        return 0;
    }
    vfs_dentry_t *parent = dentry->parent;
    if (!parent || !parent->inode) return 0;
    if (!vfs_dirent_remove(parent->inode, dentry->name)) return 0;
    dentry->inode->mode = 0;
    return 1;
}
