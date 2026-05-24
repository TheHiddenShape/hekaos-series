#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/*
 * https://wiki.osdev.org/Paging
 * https://wiki.osdev.org/Setting_Up_Paging
 */

/* PTE flags — memory rights */
#define PAGE_PRESENT 0x01
#define PAGE_RW 0x02
#define PAGE_USER 0x04

/* user/kernel classic space split (3GB/1GB) */
#define KERNEL_VIRT_BASE 0xC0000000

#define PAGE_SIZE 4096

#define USER_CODE_BASE                                                         \
    0x08048000u /* Linux i386 convention, good startpoint for future ELF       \
                   loader */
#define USER_STACK_TOP KERNEL_VIRT_BASE

void paging_init (void);
uint32_t paging_proc_init (uint32_t pid);
void paging_proc_switch (uint32_t pid);

void load_page_directory (uint32_t *page_directory);
void enable_paging (void);
uint32_t read_cr0 (void);
uint32_t read_cr2 (void);
uint32_t read_cr3 (void);
void flush_tlb (uint32_t addr);

void *get_physaddr (void *virtualaddr);
void map_page (void *physaddr, void *virtualaddr, uint32_t flags);
void unmap_page (void *virtualaddr);

void *alloc_page (void *virtualaddr, uint32_t flags);
void free_page (void *virtualaddr);

int paging_fork_copy (uint32_t child_pid);
int paging_load_user_image (uint32_t pid, const void *src, uint32_t size);

/* free every user PT + PTE-pointed frame of pid. CR3 must not be pid's pgdir.
 */
void paging_proc_teardown (uint32_t pid);

void paging_test (void);

#endif
