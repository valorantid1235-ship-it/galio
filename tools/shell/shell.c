/* shell.c - Interactive kernel shell (POLLING MODE, NO IRQs) */
#include "shell.h"
#include "vga.h"
#include "kprintf.h"
#include "string.h"
#include <string.h>
#include <stddef.h>
#include "cpu.h"
#include "vfs.h"
#include "auth.h"
#define SHELL_BUFFER_SIZE 256
#define HISTORY_SIZE 10
#define HISTORY_BUFFER_SIZE 256
#define DIR_HISTORY_SIZE 32
#define DIR_PATH_SIZE 256
#define ROOT_DIR "/"
#define HOME_DIR "/home"

/* ASCII lookup table for scancodes */
static const u8 ascii_table[] = {
    0,  27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
};

typedef struct {
    char buffer[SHELL_BUFFER_SIZE];
    u32 len;
} shell_input_t;

typedef struct {
    char history[HISTORY_SIZE][HISTORY_BUFFER_SIZE];
    u32 count;
    u32 index;
} shell_history_t;

typedef struct {
    char stack[DIR_HISTORY_SIZE][DIR_PATH_SIZE];
    u32 sp;
} dir_history_t;

static shell_input_t input;
static shell_history_t history = {0};
static dir_history_t dir_history = {0};
static u8 extended_key = 0;
static char current_dir[256] = HOME_DIR;

static void shell_add_history(const char *cmd) {
    if (input.len == 0) return;
    u32 idx = history.count % HISTORY_SIZE;
    strncpy(history.history[idx], cmd, HISTORY_BUFFER_SIZE - 1);
    history.history[idx][HISTORY_BUFFER_SIZE - 1] = 0;
    if (history.count < HISTORY_SIZE) history.count++;
    history.index = history.count;
}

static void shell_clear_line(void) {
    for (u32 i = 0; i < input.len; i++) {
        vga_putch('\b');
        vga_putch(' ');
        vga_putch('\b');
    }
}

static void shell_print_buffer(void) {
    for (u32 i = 0; i < input.len; i++) {
        vga_putch(input.buffer[i]);
    }
}

/* Parse and execute command */
static void shell_execute_command(void) {
    if (input.len == 0) return;

    input.buffer[input.len] = 0;
    shell_add_history(input.buffer);
    kprintf("\n");

    /* Handle rex (sudo-like) commands */
    if (strncmp(input.buffer, "rex ", 4) == 0) {
        if (!auth_is_authorized()) {
            char password[INPUT_BUFFER_SIZE];
            if (!auth_prompt_password("Password: ", password, INPUT_BUFFER_SIZE) ||
                !auth_verify_password(kernel_auth.username, password)) {
                kprintf("\n[REX] Access denied: Invalid password\n");
                kprintf("[@~G ->  %s] ", current_dir);
                input.len = 0;
                return;
            }

            auth_authorize();
            kprintf("\n[REX] Password accepted. Privileged mode enabled.\n");
        }

        const char *cmd = input.buffer + 4;
        if (strncmp(cmd, "goto ", 5) == 0) {
            const char *path = cmd + 5;
            if (path[0] == '/') {
                strncpy(current_dir, path, 255);
            } else {
                char fullpath[256];
                strncpy(fullpath, current_dir, 255);
                fullpath[255] = 0;
                int len = strlen(fullpath);
                if (len > 0 && fullpath[len-1] != '/') {
                    strncat(fullpath, "/", 255 - len - 1);
                }
                strncat(fullpath, path, 255 - strlen(fullpath) - 1);
                strncpy(current_dir, fullpath, 255);
            }
            current_dir[255] = 0;
            kprintf("[REX] Changed to: %s\n", current_dir);
        } else {
            kprintf("[REX] Unknown privileged command: %s\n", cmd);
        }
    } else if (strncmp(input.buffer, "clear", 5) == 0) {
        vga_clear();
        kprintf("                                GSH                                         \n");
        kprintf("                                                                             \n");
        kprintf("                                                                             \n");
        kprintf("                                                                             \n");
       // kprintf("_____________________________________________________________________________");
    }
    
    else if (strncmp(input.buffer, "help", 4) == 0) {
        kprintf("\n____________________________________________________________________\n");
        kprintf(" |                     GSH  - Available Commands:                   |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  ls       - List directory contents                              |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  mkdir    - Create directory (usage: mkdir <path>)               |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  rmdir    - Remove directory (usage: rmdir <path>)               |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" | clear    - Clear the screen                                      |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  echo     - Echo text (usage: echo <text>)                       |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  uname    - Show system name                                     |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" | pwd      - Print current directory                               |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  goto     - Change directory (usage: goto <path>)                |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  back     - Go back to previous directory                        |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  rex      - Privileged command                                   |\n");
        kprintf(" |            gain full access of your device.                      |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |  Use UP/DOWN arrows to navigate history                          |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf(" |      *****    NEXT TIME HELP YOURSELF    *****                   |\n");
        kprintf(" |__________________________________________________________________|\n");
        kprintf("                                                                      \n");
        kprintf("                                                                      \n");
    } else if (strncmp(input.buffer, "ls", 2) == 0) {
        vfs_listdir(current_dir);
    } else if (strncmp(input.buffer, "mkdir ", 6) == 0) {
        const char *dirname = input.buffer + 6;
        char fullpath[256];

        if (dirname[0] == '/') {
            strncpy(fullpath, dirname, 255);
            fullpath[255] = 0;
        } else {
            strncpy(fullpath, current_dir, 255);
            fullpath[255] = 0;
            int len = strlen(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                strncat(fullpath, "/", 255 - len - 1);
            }
            strncat(fullpath, dirname, 255 - strlen(fullpath) - 1);
        }
        //kprintf("[DEBUG] mkdir: current_dir='%s', dirname='%s', fullpath='%s'\n", current_dir, dirname, fullpath);
        vfs_mkdir(fullpath);
    } else if (strncmp(input.buffer, "rmdir ", 6) == 0) {
        const char *dirname = input.buffer + 6;
        char fullpath[256];

        if (dirname[0] == '/') {
            strncpy(fullpath, dirname, 255);
            fullpath[255] = 0;
        } else {
            strncpy(fullpath, current_dir, 255);
            fullpath[255] = 0;
            int len = strlen(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                strncat(fullpath, "/", 255 - len - 1);
            }
            strncat(fullpath, dirname, 255 - strlen(fullpath) - 1);
        }
        vfs_rmdir(fullpath);
    } else if (strncmp(input.buffer, "pwd", 3) == 0) {
        kprintf("%s\n", current_dir);
    } else if (strncmp(input.buffer, "goto ", 5) == 0) {
        const char *dirname = input.buffer + 5;
        char fullpath[256];

        if (dirname[0] == '/') {
            strncpy(fullpath, dirname, 255);
            fullpath[255] = 0;
        } else {
            strncpy(fullpath, current_dir, 255);
            fullpath[255] = 0;
            int len = strlen(fullpath);
            if (len > 0 && fullpath[len-1] != '/') {
                strncat(fullpath, "/", 255 - len - 1);
            }
            strncat(fullpath, dirname, 255 - strlen(fullpath) - 1);
        }

        if (strcmp(fullpath, ROOT_DIR) == 0) {
            kprintf("Permission denied: use 'rex goto /' to access root\n");
        } else if (vfs_is_dir(fullpath)) {
            if (dir_history.sp < DIR_HISTORY_SIZE) {
                strncpy(dir_history.stack[dir_history.sp], current_dir, DIR_PATH_SIZE - 1);
                dir_history.stack[dir_history.sp][DIR_PATH_SIZE - 1] = 0;
                dir_history.sp++;
            }
            strncpy(current_dir, fullpath, 255);
            current_dir[255] = 0;
        } else {
            kprintf("Directory not found: %s\n", fullpath);
        }
    } else if (strncmp(input.buffer, "back", 4) == 0) {
        if (dir_history.sp > 0) {
            dir_history.sp--;
            strncpy(current_dir, dir_history.stack[dir_history.sp], 255);
            current_dir[255] = 0;
        } else {
            kprintf("No previous directory\n");
        }
    } else if (strncmp(input.buffer, "echo ", 5) == 0) {
        kprintf("%s\n", input.buffer + 5);
    } else if (strncmp(input.buffer, "uname", 5) == 0) {
        kprintf("Galio v1.0\n");
    } else if (input.len > 0) {
        kprintf("Unknown command: %s\nType 'help' for available commands\n", input.buffer);
    }

    kprintf("[@~G ->  %s] ", current_dir);
    input.len = 0;
}

/* Poll keyboard for input (no IRQs) */
static void shell_poll_keyboard(void) {
    u8 status = inb(0x64);

    if (status & 0x01) {
        u8 scancode = inb(0x60);

        if (scancode == 0xE0) {
            extended_key = 1;
            return;
        }

        u8 is_pressed = !(scancode & 0x80);
        u8 raw_scancode = scancode & 0x7F;

        if (!is_pressed) {
            if (extended_key) extended_key = 0;
            return;
        }

        if (extended_key) {
            extended_key = 0;
            if (raw_scancode == 0x48) {
                if (history.index > 0) {
                    history.index--;
                    shell_clear_line();
                    strncpy(input.buffer, history.history[history.index], SHELL_BUFFER_SIZE - 1);
                    input.len = strlen(input.buffer);
                    shell_print_buffer();
                }
                return;
            } else if (raw_scancode == 0x50) {
                if (history.index < history.count - 1) {
                    history.index++;
                    shell_clear_line();
                    strncpy(input.buffer, history.history[history.index], SHELL_BUFFER_SIZE - 1);
                    input.len = strlen(input.buffer);
                    shell_print_buffer();
                } else if (history.index == history.count - 1) {
                    history.index = history.count;
                    shell_clear_line();
                    input.len = 0;
                }
                return;
            }
            return;
        }

        if (raw_scancode >= sizeof(ascii_table)) return;

        u8 c = ascii_table[raw_scancode];
        if (c == 0) return;

        if (c == '\b') {
            if (input.len > 0) {
                input.len--;
                vga_putch('\b');
                vga_putch(' ');
                vga_putch('\b');
            }
        } else if (c == '\n') {
            vga_putch('\n');
            shell_execute_command();
        } else if (c == '\t') {
            return;
        } else if (c >= 32 && c < 127) {
            if (input.len < SHELL_BUFFER_SIZE - 1) {
                input.buffer[input.len] = c;
                input.len++;
                vga_putch(c);
            }
        }
    }
}

void shell_run(void) {
    input.len = 0;

    vga_clear();

    strncpy(current_dir, HOME_DIR, sizeof(current_dir) - 1);
    current_dir[sizeof(current_dir) - 1] = 0;

    kprintf("                       Welcome to GSh !                                      \n");
    kprintf("_____________________________________________________________________________");
    kprintf("                                                                             \n");
    kprintf("                                                                             \n");
    kprintf("                                                                             \n");
    kprintf(" ~[G] <%s>  ", current_dir);

    for (;;) {
        shell_poll_keyboard();
        for (volatile int i = 0; i < 100; i++);
    }
}
