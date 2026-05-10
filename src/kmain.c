#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "irq.h"
#include "kprintf.h"
#include "serial.h"
#include "pmem.h"
#include "paging.h"
#include "heap.h"
#include "pit.h"
#include "keyboard.h"
#include "process.h"
#include "vfs.h"
#include "elf.h"
#include "shell.h"
#include "cpu.h"
#include "auth.h"

/* Syscall interface declaration */
void syscall_init(void);

/* Memory test declaration */
void mem_test_run(void);

/* Embedded test binary */
extern u8 _binary_test_elf_bin_start;
extern u8 _binary_test_elf_bin_end;

/* Entry point from bootloader - receives Multiboot info */
void kmain(void *multiboot_ptr) {
    (void)multiboot_ptr;

    serial_init();
    kprintf("=== Galio Kernel Boot ===\n\n");

    kprintf("Initializing VGA...\n");
    vga_init();

    kprintf("Initializing GDT...\n");
    gdt_init();

    kprintf("Initializing IDT...\n");
    idt_init();

    kprintf("Installing IRQ handlers...\n");
    irq_install();

    kprintf("Initializing physical memory manager...\n");

    typedef struct {
        u32 flags;
        u32 mem_lower;
        u32 mem_upper;
        u32 boot_device;
        u32 cmdline;
        u32 mods_count;
        u32 mods_addr;
        u32 syms[4];
        u32 mmap_length;
        u32 mmap_addr;
    } multiboot_info_t;

    multiboot_info_t *mb_info = (multiboot_info_t *)multiboot_ptr;
    u32 mmap_addr = 0;
    u32 mmap_length = 0;

    if (mb_info && (mb_info->flags & (1 << 6))) {
        mmap_addr = mb_info->mmap_addr;
        mmap_length = mb_info->mmap_length;
        kprintf("Found Multiboot mmap: addr=%x len=%u\n", mmap_addr, mmap_length);
    } else {
        kprintf("No Multiboot mmap available, using fallback\n");
    }

    pmem_init(mmap_addr, mmap_length);

    /* Paging and heap disabled for now */
    kprintf("Initializing paging...\n");
    paging_init();
    kprintf("Initializing heap...\n");
    heap_init();

    kprintf("Running memory stabilization tests...\n");
    mem_test_run();

    kprintf("Initializing process manager...\n");
    process_init();

    kprintf("Installing system call handler...\n");
    syscall_init();

    kprintf("Initializing timer (100 Hz)...\n");
    pit_init(100);

    /* Do NOT initialize keyboard driver – we will use polling in shell */
    // kprintf("Initializing keyboard...\n");
    // keyboard_init();

    kprintf("Initializing filesystem...\n");

    extern u8 _binary_initrd_bin_start;
    vfs_init(&_binary_initrd_bin_start);
    vfs_debug();

    /* Test filesystem output - reduced for clarity */
    kprintf("\n");
    kprintf("╔══════════════════════════════════════════════════════════════╗\n");
    kprintf("║           FILESYSTEM VERIFICATION & DEBUG OUTPUT             ║\n");
    kprintf("╚══════════════════════════════════════════════════════════════╝\n");
    kprintf("\n");

    kprintf("[TEST 1] Filesystem Statistics\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_stats();
    kprintf("\n");

    kprintf("[TEST 2] Root Directory Contents\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    vfs_listdir("/");
    kprintf("\n");

    kprintf("[TEST 3] Reading /etc/hostname\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    char hostname_buf[256];
    u32 bytes_read = vfs_read("/etc/hostname", hostname_buf, 255);
    if (bytes_read > 0) {
        hostname_buf[bytes_read] = 0;
        kprintf("Successfully read %u bytes\n", bytes_read);
        kprintf("  Content: %s", hostname_buf);
    } else {
        kprintf("FAILED to read /etc/hostname\n");
    }
    kprintf("\n");

    kprintf("[TEST 4] Reading /boot/config.txt\n");
    kprintf("─────────────────────────────────────────────────────────────\n");
    char config_buf[512];
    bytes_read = vfs_read("/boot/config.txt", config_buf, 511);
    if (bytes_read > 0) {
        config_buf[bytes_read] = 0;
        kprintf("Successfully read %u bytes\n", bytes_read);
        kprintf("  Content:\n%s", config_buf);
    } else {
        kprintf("FAILED to read /boot/config.txt\n");
    }
    kprintf("\n");
    kprintf("Filesystem tests completed.\n");
    kprintf("─────────────────────────────────────────────────────────────\n");

    auth_bootstrap();

    vga_clear();
    kprintf("Welcome to Galio !\n");
    kprintf("Press c to enter GSH....\n\n");

    /* ELF test disabled - needs proper kernel virtual address mapping */
    // kprintf("Loading ELF loader smoke test...\n");
    // u32 elf_entry = elf_load(&_binary_test_elf_bin_start);
    // if (elf_entry) {
    //     kprintf("ELF loaded successfully, entry point: %08X\n", elf_entry);
    //     kprintf("Executing test ELF...\n");
    //     ((void (*)(void))elf_entry)();
    // } else {
    //     kprintf("ELF_LOADER_TEST: FAILED\n");
    // }
         

    /* Disable all interrupts – we will use polling */
    __asm__ volatile("cli");

    /* Mask all IRQs on both PICs */
    outb(0x21, 0xFF);  /* Master PIC */
    outb(0xA1, 0xFF);  /* Slave PIC */

    /* Poll for 'c' key press using direct port I/O */
   // kprintf("Waiting for 'c' key...\n");
    
    while (1) {
        u8 status = inb(0x64);
        
        if (status & 0x01) {
            u8 scancode = inb(0x60);
            
            if (scancode == 0x2E) {  /* 'c' key */
                /* Drain keyboard buffer */
                while (inb(0x64) & 0x01) {
                    inb(0x60);
                }
                kprintf("\n'c' key detected! Launching shell...\n\n");
                break;
            }
        }
        
        /* Small delay to avoid busy loop */
        for (volatile int i = 0; i < 1000; i++);
    }

    /* Launch interactive shell (polling mode) */
    shell_run();

    /* Should never reach here */
    for(;;);
}