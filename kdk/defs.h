#ifndef KRX_KDK_TYPES_H
#define KRX_KDK_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/*! Virtual/physical addresses. */
typedef uintptr_t vaddr_t, paddr_t;
/*! Page number. */
typedef uintptr_t pfn_t;
/*! Page offset. */
typedef intptr_t pgoff_t;

#define LOCK_REQUIRES(LOCK)
#define LOCK_EXCLUDES(LOCK)

#endif /* KRX_KDK_TYPES_H */
