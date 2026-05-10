#include "commands/file.h"
#include "kprintf.h"
#include "string.h"
#include "vfs.h"

static const char *default_home_path(void) {
    return "/home";
}

static void safe_strcat(char *dest, const char *src, u32 max_len) {
    u32 dest_len = strlen(dest);
    u32 copy_len = max_len - dest_len - 1;
    if (copy_len > 0) {
        strncat(dest, src, copy_len);
        dest[max_len - 1] = 0;
    }
}

static u8 has_extension(const char *filename) {
    while (*filename) {
        if (*filename == '.') return 1;
        filename++;
    }
    return 0;
}

static void ensure_extension(char *filename, u32 max_len) {
    if (!has_extension(filename)) {
        safe_strcat(filename, ".txt", max_len);
    }
}

static void build_target_path(const char *dir, const char *filename, char *out_path) {
    u32 len = strlen(dir);
    if (len == 0) {
        strncpy(out_path, "/", VFS_MAX_PATH - 1);
        out_path[VFS_MAX_PATH - 1] = 0;
    } else {
        strncpy(out_path, dir, VFS_MAX_PATH - 1);
        out_path[VFS_MAX_PATH - 1] = 0;
    }

    if (out_path[strlen(out_path) - 1] != '/') {
        safe_strcat(out_path, "/", VFS_MAX_PATH);
    }
    safe_strcat(out_path, filename, VFS_MAX_PATH);
}

static u8 build_path_and_filename(const char *args, const char *current_dir, char *out_fullpath) {
    char local[512];
    char filename[VFS_MAX_FILENAME];
    char target_dir[VFS_MAX_PATH];
    char *path_token = NULL;

    strncpy(local, args, sizeof(local) - 1);
    local[sizeof(local) - 1] = 0;

    char *space = local;
    while (*space && *space != ' ') {
        space++;
    }

    if (*space == ' ') {
        *space = 0;
        path_token = space + 1;
        while (*path_token == ' ') path_token++;
        if (*path_token == 0) {
            path_token = NULL;
        }
    }

    if (local[0] == 0) {
        return 0;
    }

    strncpy(filename, local, sizeof(filename) - 1);
    filename[sizeof(filename) - 1] = 0;

    if (filename[0] == '/') {
        strncpy(out_fullpath, filename, VFS_MAX_PATH - 1);
        out_fullpath[VFS_MAX_PATH - 1] = 0;
        ensure_extension(out_fullpath, VFS_MAX_PATH);
        return 1;
    }

    ensure_extension(filename, sizeof(filename));

    if (!path_token) {
        strncpy(target_dir, default_home_path(), VFS_MAX_PATH - 1);
        target_dir[VFS_MAX_PATH - 1] = 0;
    } else if (path_token[0] == '/') {
        strncpy(target_dir, path_token, VFS_MAX_PATH - 1);
        target_dir[VFS_MAX_PATH - 1] = 0;
    } else {
        strncpy(target_dir, current_dir, VFS_MAX_PATH - 1);
        target_dir[VFS_MAX_PATH - 1] = 0;
        if (strlen(target_dir) > 0 && target_dir[strlen(target_dir) - 1] != '/') {
            safe_strcat(target_dir, "/", VFS_MAX_PATH);
        }
        safe_strcat(target_dir, path_token, VFS_MAX_PATH);
    }

    build_target_path(target_dir, filename, out_fullpath);
    return 1;
}

u8 shell_file_command(const char *args, const char *current_dir, u8 replace) {
    if (!args || *args == 0) {
        kprintf("[FILE] Usage: file <name>[.ext] [path]\n");
        kprintf("[FILE] Example: file notes /home/Documents\n");
        return 0;
    }

    char fullpath[VFS_MAX_PATH];
    if (!build_path_and_filename(args, current_dir, fullpath)) {
        kprintf("[FILE] Invalid arguments. Use: file <name>[.ext] [path]\n");
        return 0;
    }

    return vfs_create(fullpath, replace);
}
