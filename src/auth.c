/* auth.c - User authentication system */
#include "auth.h"
#include "vga.h"
#include "kprintf.h"
#include "string.h"
#include "cpu.h"
#include <string.h>
#include <stddef.h>

#define INPUT_BUFFER_SIZE 32

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

/* Simple password verification - using hardcoded defaults */
u8 auth_verify_password(const char *username, const char *password) {
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
    kprintf("║             Welcome to Galio Kernel Shell                   ║\n");
    kprintf("║                   Please Log In                              ║\n");
    kprintf("╚══════════════════════════════════════════════════════════════╝\n");
    kprintf("\n");
}

void auth_login(user_session_t *session) {
    char username[INPUT_BUFFER_SIZE];
    char password[INPUT_BUFFER_SIZE];
    u32 attempts = 0;
    const u32 MAX_ATTEMPTS = 3;

    auth_show_login_prompt();

    while (attempts < MAX_ATTEMPTS) {
        kprintf("Username: ");
        read_line(username, INPUT_BUFFER_SIZE, 1);  /* Echo username */

        kprintf("Password: ");
        read_line(password, INPUT_BUFFER_SIZE, 0);  /* Hide password */

        if (auth_verify_password(username, password)) {
            strncpy(session->username, username, 31);
            session->username[31] = 0;
            strncpy(session->password, password, 31);
            session->password[31] = 0;
            session->authenticated = 1;
            kprintf("\n[AUTH] Welcome %s!\n\n", username);
            return;
        }

        attempts++;
        kprintf("[AUTH] Invalid credentials! Attempts remaining: %u\n\n", MAX_ATTEMPTS - attempts);
    }

    /* Max attempts exceeded */
    kprintf("[AUTH] Maximum login attempts exceeded. System halting.\n");
    for(;;) __asm__ volatile("cli; hlt");
}
