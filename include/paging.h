#ifndef PAGING_H
#define PAGING_H
/* Page table entry flags */
#define PAGE_PRESENT      0x001
#define PAGE_RW           0x002
#define PAGE_USER         0x004
#define PAGE_WRITETHROUGH 0x008
#define PAGE_NOCACHE      0x010
#define PAGE_ACCESSED     0x020
#define PAGE_DIRTY        0x040

#include "common.h"

/* Paging structures and operations */

typedef struct {
    u32 *directory;
    u32 *tables[1024];
} page_directory_t;

/* Page table entry flags */
#define PAGE_PRESENT      0x001
#define PAGE_RW           0x002
#define PAGE_USER         0x004
#define PAGE_WRITETHROUGH 0x008
#define PAGE_NOCACHE      0x010
#define PAGE_ACCESSED     0x020
#define PAGE_DIRTY        0x040

/* Initialize paging system */
void paging_init(void);

/* Create a new page directory */
page_directory_t *paging_create_directory(void);

/* Map a virtual address to a physical address */
void paging_map(page_directory_t *pd, u32 vaddr, u32 paddr, u32 flags);

/* Unmap a virtual address */
void paging_unmap(page_directory_t *pd, u32 vaddr);

/* Get physical address for virtual address */
u32 paging_get_physical(page_directory_t *pd, u32 vaddr);

/* Enable paging */
void paging_enable(page_directory_t *pd);

/* Get current page directory */
page_directory_t *paging_get_current(void);

#endif /* PAGING_H */
