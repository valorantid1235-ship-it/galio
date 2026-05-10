#include "commands/new.h"
#include "commands/file.h"
#include "kprintf.h"
#include "string.h"

u8 shell_new_command(const char *args, const char *current_dir) {
    if (!args || *args == 0) {
        kprintf("[NEW] Usage: new file <name>[.ext] [path]\n");
        kprintf("[NEW] The 'new' command is a generic creation prefix for future objects.\n");
        return 0;
    }

    if (strncmp(args, "file ", 5) == 0) {
        return shell_file_command(args + 5, current_dir, 1);
    }

    kprintf("[NEW] Unknown target for new: %s\n", args);
    kprintf("[NEW] Supported today: new file <name>[.ext] [path]\n");
    return 0;
}
