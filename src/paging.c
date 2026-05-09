/* paging.c - Virtual memory paging */
#include "paging.h"
#include "pmem.h"
#include "kprintf.h"

#define PAGE_SIZE 4096
#define TABLE_SIZE 1024

static page_directory_t kernel_pd_storage;
static page_directory_t *kernel_pd = NULL;

/* Helper: allocate and clear a page table */
static u32 alloc_page_table(void) {
    u32 pt_phys = pmem_alloc(1);
    if (!pt_phys) return 0;
    volatile u32 *pt_virt = (volatile u32 *)pt_phys;
    for (int i = 0; i < 1024; i++) pt_virt[i] = 0;
    return pt_phys;
}

void paging_init(void) {
    kprintf("Initializing paging system...\n");
    kernel_pd = paging_create_directory();
    if (!kernel_pd) panic("Paging directory creation failed");

    /* Identity map first 16 MB (0x0 - 0x1000000) for now */
    kprintf("Identity-mapping first 16 MB...\n");
    for (u32 base = 0; base < 0x1000000; base += 0x400000) {
        u32 pt_phys = alloc_page_table();
        if (!pt_phys) panic("Failed to allocate page table");
        volatile u32 *pt = (volatile u32 *)pt_phys;
        for (u32 i = 0; i < 1024; i++) {
            u32 paddr = base + i * PAGE_SIZE;
            pt[i] = paddr | PAGE_PRESENT | PAGE_RW;
        }
        u32 pde_index = base >> 22;
        kernel_pd->directory[pde_index] = (pt_phys & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW;
        kernel_pd->tables[pde_index] = (u32 *)pt_phys;
    }

    kprintf("Paging: mapped first 16 MB\n");
    paging_enable(kernel_pd);
    kprintf("Paging enabled successfully\n");
}

page_directory_t *paging_create_directory(void) {
    u32 pd_phys = pmem_alloc(1);
    if (!pd_phys) return NULL;
    volatile u32 *pd_virt = (volatile u32 *)pd_phys;
    for (int i = 0; i < 1024; i++) pd_virt[i] = 0;
    kernel_pd_storage.directory = (u32 *)pd_phys;
    for (int i = 0; i < 1024; i++) kernel_pd_storage.tables[i] = NULL;
    return &kernel_pd_storage;
}

void paging_map(page_directory_t *pd, u32 vaddr, u32 paddr, u32 flags) {
    u32 pd_idx = (vaddr >> 22) & 0x3FF;
    u32 pt_idx = (vaddr >> 12) & 0x3FF;

    if (!pd->tables[pd_idx]) {
        u32 pt_phys = alloc_page_table();
        if (!pt_phys) {
            kprintf("paging_map: Failed to allocate page table\n");
            return;
        }
        pd->tables[pd_idx] = (u32 *)pt_phys;
        pd->directory[pd_idx] = (pt_phys & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW;
    }

    u32 pte = (paddr & 0xFFFFF000) | flags;
    pd->tables[pd_idx][pt_idx] = pte;
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

void paging_unmap(page_directory_t *pd, u32 vaddr) {
    u32 pd_idx = (vaddr >> 22) & 0x3FF;
    u32 pt_idx = (vaddr >> 12) & 0x3FF;
    if (pd->tables[pd_idx]) {
        pd->tables[pd_idx][pt_idx] = 0;
        __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
    }
}

u32 paging_get_physical(page_directory_t *pd, u32 vaddr) {
    u32 pd_idx = (vaddr >> 22) & 0x3FF;
    u32 pt_idx = (vaddr >> 12) & 0x3FF;
    if (!pd->tables[pd_idx]) return 0;
    u32 pte = pd->tables[pd_idx][pt_idx];
    if (!(pte & PAGE_PRESENT)) return 0;
    return (pte & 0xFFFFF000) | (vaddr & 0xFFF);
}

void paging_enable(page_directory_t *pd) {
    u32 pd_phys = (u32)pd->directory;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
    u32 cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

page_directory_t *paging_get_current(void) {
    return kernel_pd;
}