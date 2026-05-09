/* shell.c - Interactive kernel shell (POLLING MODE, NO IRQs) */
#include "shell.h"
#include "vga.h"
#include "kprintf.h"
#include "string.h"
#include <stddef.h>
#include "cpu.h"
#define SHELL_BUFFER_SIZE 256

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

static shell_input_t input;

/* Parse and execute command */
static void shell_execute_command(void) {
    if (input.len == 0) return;
    
    input.buffer[input.len] = 0;
    kprintf("\n");
    
    if (strncmp(input.buffer, "clear", 5) == 0) {
        vga_clear();
    } else if (strncmp(input.buffer, "help", 4) == 0) {
        kprintf("Galio Kernel Shell - Available Commands:\n");
        kprintf("  help     - Next time, help yourself.\n");
        kprintf("  clear    - Clear the screen\n");
        kprintf("  echo     - Echo text (usage: echo <text>)\n");
        kprintf("  uname    - Show system name\n");
        kprintf("  test     - Run a simple test\n");
    } else if (strncmp(input.buffer, "echo ", 5) == 0) {
        kprintf("%s\n", input.buffer + 5);
    } else if (strncmp(input.buffer, "uname", 5) == 0) {
        kprintf("Galio Kernel v1.0\n");
    } else if (strncmp(input.buffer, "test", 4) == 0) {
        kprintf("Test: You typed: %s\n", input.buffer);
    } else if (input.len > 0) {
        kprintf("Unknown command: %s\nType 'help' for available commands\n", input.buffer);
    }
    
    kprintf("> ");
    input.len = 0;
}

/* Poll keyboard for input (no IRQs) */
static void shell_poll_keyboard(void) {
    u8 status = inb(0x64);
    
    /* Check if data is available */
    if (status & 0x01) {
        u8 scancode = inb(0x60);
        u8 is_pressed = !(scancode & 0x80);
        u8 raw_scancode = scancode & 0x7F;
        
        if (!is_pressed) return;
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

/* Main shell function - polling mode, no IRQs */
void shell_run(void) {
    input.len = 0;
    
    /* Clear screen and show prompt */
    vga_clear();
    kprintf("Galio Kernel Shell - Type 'help' for commands\n");
    kprintf("> ");
    
    /* Main polling loop - no interrupts needed */
    for (;;) {
        shell_poll_keyboard();
        
        /* Small delay to avoid busy loop */
        for (volatile int i = 0; i < 100; i++);
    }
}