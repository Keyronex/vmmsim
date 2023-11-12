#ifndef KRX_KDK_PLATFORM_H
#define KRX_KDK_PLATFORM_H

#include <stdint.h>

#define KRX_PLATFORM_BITS 64

#define PGSIZE 4096
#define V2P(VALUE) (((vaddr_t)(VALUE)) - (vaddr_t)SOFT_pages)
#define P2V(VALUE) (((vaddr_t)(VALUE)) + (vaddr_t)SOFT_pages)

#define vm_page_direct_map_addr(PAGE) P2V(vmp_page_paddr(PAGE))

#define SOFT_NPAGES 32

extern uint8_t SOFT_pages[4096 * SOFT_NPAGES];

#endif /* KRX_KDK_PLATFORM_H */
