#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/*
https://wiki.osdev.org/Paging
https://wiki.osdev.org/Setting_Up_Paging
*/

// PTE flags — memory rights
#define PAGE_PRESENT    0x01
#define PAGE_RW         0x02
#define PAGE_USER       0x04

// User/Kernel classic space split (3GB/1GB)
#define KERNEL_VIRT_BASE    0xC0000000
#define USER_SPACE_TOP      KERNEL_VIRT_BASE

#define PAGE_SIZE 4096

#define PD_ENTRIES 1024
#define PT_ENTRIES 1024

#define PT_POOL_SIZE 16
#define PT_POOL_ENTRIES PT_ENTRIES

void paging_init (void);

void load_page_directory (uint32_t *page_directory);
void enable_paging (void);
uint32_t read_cr0 (void);
uint32_t read_cr2 (void);
uint32_t read_cr3 (void);
void flush_tlb (unsigned long addr);

void *get_physaddr (void *virtualaddr);
void map_page (void *physaddr, void *virtualaddr, unsigned int flags);
void unmap_page (void *virtualaddr);

void paging_test (void);

#endif
