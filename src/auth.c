/* auth.c - User authentication system */
#include "auth.h"
#include "vga.h"
#include "kprintf.h"
#include "string.h"
#include "cpu.h"
#include <string.h>
#include <stddef.h>

user_session_t kernel_auth = {0};

static u8 read_char(void) {
    while (1) {
        u8 status = inb(0x64);
        if (status & 0x01) {
            u8 scancode = inb(0x60);
            if (scancode & 0x80) continue;  /* Key release */

            static const u8 ascii_table[] = {
                0,  27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
                'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0, 'a', 's',
                'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
                'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
            };

            if (scancode < sizeof(ascii_table)) {
                u8 c = ascii_table[scancode];
                if (c != 0 && c != '\t') {
                    return c;
                }
            }
        }
        for (volatile int i = 0; i < 100; i++);
    }
}

static void read_line(char *buffer, u32 max_len, u8 echo) {
    u32 len = 0;
    while (len < max_len - 1) {
        u8 c = read_char();

        if (c == '\n') {
            buffer[len] = 0;
            kprintf("\n");
            break;
        } else if (c == '\b') {
            if (len > 0) {
                len--;
                kprintf("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            buffer[len++] = c;
            if (echo) {
                vga_putch(c);
            } else {
                vga_putch('*');  /* Mask password */
            }
        }
    }
    buffer[max_len - 1] = 0;
}

/* Simple password verification - using boot-time registration or hardcoded defaults */
u8 auth_verify_password(const char *username, const char *password) {
    if (kernel_auth.registered) {
        return strcmp(username, kernel_auth.username) == 0 && strcmp(password, kernel_auth.password) == 0;
    }

    /* Default credentials for testing - change in production */
    if (strcmp(username, "galio") == 0 && strcmp(password, "galio") == 0) {
        return 1;
    }
    if (strcmp(username, "root") == 0 && strcmp(password, "root") == 0) {
        return 1;
    }
    if (strcmp(username, "admin") == 0 && strcmp(password, "admin123") == 0) {
        return 1;
    }
    return 0;
}

void auth_show_login_prompt(void) {
    kprintf("\n");
    kprintf("╔══════════════════════════════════════════════════════════════╗\n");
    kprintf("║           Galio Kernel Registration                          ║\n");
    kprintf("║          Create a kernel account for privileged access       ║\n");
    kprintf("╚══════════════════════════════════════════════════════════════╝\n");
    kprintf("\n");
}

void auth_bootstrap(void) {
    if (kernel_auth.registered) {
        return;
    }

    char username[INPUT_BUFFER_SIZE];
    char password[INPUT_BUFFER_SIZE];
    char confirm[INPUT_BUFFER_SIZE];

    auth_show_login_prompt();

    while (1) {
        kprintf("Username: ");
        read_line(username, INPUT_BUFFER_SIZE, 1);

        kprintf("Password: ");
        read_line(password, INPUT_BUFFER_SIZE, 0);

        kprintf("Confirm Password: ");
        read_line(confirm, INPUT_BUFFER_SIZE, 0);

        if (username[0] == 0 || password[0] == 0) {
            kprintf("[AUTH] Username and password cannot be empty. Try again.\n\n");
            continue;
        }
        if (strcmp(password, confirm) != 0) {
            kprintf("[AUTH] Passwords do not match. Try again.\n\n");
            continue;
        }

        strncpy(kernel_auth.username, username, sizeof(kernel_auth.username) - 1);
        kernel_auth.username[sizeof(kernel_auth.username) - 1] = 0;
        strncpy(kernel_auth.password, password, sizeof(kernel_auth.password) - 1);
        kernel_auth.password[sizeof(kernel_auth.password) - 1] = 0;
        kernel_auth.registered = 1;
        kernel_auth.authenticated = 0;

        kprintf("\n[AUTH] Kernel account registered for user '%s'.\n", kernel_auth.username);
        kprintf("[AUTH] Use 'rex' in the shell to run privileged commands.\n\n");
        return;
    }
}

u8 auth_prompt_password(const char *prompt, char *password, u32 max_len) {
    kprintf("%s", prompt);
    read_line(password, max_len, 0);
    return password[0] != 0;
}

u8 auth_is_authorized(void) {
    return kernel_auth.authenticated;
}

void auth_authorize(void) {
    kernel_auth.authenticated = 1;
}
