#ifndef KRX_VM_VMP_H
#define KRX_VM_VMP_H

#include <kdk/defs.h>
#include <kdk/queue.h>
#include <kdk/tree.h>
#include <stdbool.h>
#include <stdint.h>

#include "nanokern.h"
#include "vmpsoft.h"

typedef TAILQ_HEAD(vmp_page_queue, vm_page) vmp_page_queue_t;

struct vmp_pte_wire_state {
	struct eprocess *ps;
	pte_t *pte;
	vm_page_t *pages[VMP_TABLE_LEVELS];
};

typedef struct vmp_pager_state {
	uint32_t refcount;
	kevent_t event;
} vmp_pager_state_t;

struct vmp_forkpage {
	pte_t pte;
	uint32_t refcount;
};

typedef struct vm_section {

} vm_section_t;

typedef struct vad {
	struct vm_vad_flags {
		/*! current protection, and maximum legal protection */
		bool writeable : 1, max_protection : 1,
		    /*! whether shared on fork (if false, copied) */
		    inherit_shared : 1,
		    /*! whether mapping is private anonymous memory. */
		    private : 1,
		    /*! (!private only) whether the mapping is copy-on-write */
		    cow : 1;
		/*! if !private, page-unit offset into section (max 256tib) */
		int64_t offset : 36;
	} flags;
	/*! Entry in vm_procstate::vad_rbtree */
	RB_ENTRY(vm_vad) rb_entry;
	/*! Start and end vitrual address. */
	vaddr_t start, end;
	/*! Section object; only set if flags.private = false */
	vm_section_t *section;
} vad_t;

int vmp_page_alloc_locked(vm_page_t **out, enum vm_page_use use, bool must);
vm_page_t *vmp_page_retain_locked(vm_page_t *page);
void vmp_page_release_locked(vm_page_t *page);
vm_page_t *vmp_paddr_to_page(paddr_t paddr);

/*!
 * @brief Insert one entry into a working set list.
 * n.b. Does not trim
 * @pre WS lock held
 */
void vmp_wsl_insert(struct eprocess *ps, vaddr_t vaddr, bool locked)
    LOCK_REQUIRES(ps->ws_lock) LOCK_EXCLUDES(pfn_lock);
/*!
 * @brief Remove one entry from a working set list.
 * @pre WS lock held
 */
void vmp_wsl_remove(struct eprocess *ps, vaddr_t vaddr)
    LOCK_REQUIRES(ps->ws_lock) LOCK_EXCLUDES(pfn_lock);
/*!
 * @brief Lock an existing entry into a working set list.
 * @pre WS lock held.
 */
void vmp_wsl_lock_entry(struct eprocess *ps, vaddr_t vaddr)
    LOCK_REQUIRES(ps->ws_lock);
/*!
 * @brief Lock an existing entry into a working set list.
 * @pre WS lock held.
 */
void vmp_wsl_unlock_entry(struct eprocess *ps, vaddr_t vaddr)
    LOCK_REQUIRES(ps->ws_lock);

/*!
 * @brief Evict one entry from a working set list
 * @pre WS lock held
 * @pre PFN lock held
 */
void wsl_evict_one(struct eprocess *ps) LOCK_REQUIRES(ps->ws_lock)
    LOCK_REQUIRES(pfn_lock);

/* paddr_t vmp_page_paddr(vm_page_t *page) */
#define vmp_page_paddr(PAGE) ((paddr_t)(PAGE)->pfn << VMP_PAGE_SHIFT)

/* paddr_t vmp_pfn_to_paddr(pfn_t pfn) */
#define vmp_pfn_to_paddr(PFN) ((paddr_t)PFN << VMP_PAGE_SHIFT)

/* paddr_t vmp_pte_hw_paddr(pte_t *pte, int level) */
#define vmp_pte_hw_paddr(PTE, LVL) vmp_pfn_to_paddr(vmp_pte_hw_pfn(PTE, LVL))

/* vm_page_t *vmp_pte_hw_page(pte_t *pte, int level) */
#define vmp_pte_hw_page(PTE, LVL) vmp_paddr_to_page(vmp_pte_hw_paddr(PTE, LVL))

/* ipl_t vmp_acquire_pfn_lock(void) */
#define vmp_acquire_pfn_lock() ke_spinlock_acquire(&vmp_pfn_lock);

/* void vmp_release_pfn_lock(ipl_t ipl) */
#define vmp_release_pfn_lock(IPL) ke_spinlock_release(&vmp_pfn_lock, IPL)

extern kspinlock_t vmp_pfn_lock;

#endif /* KRX_VM_VMP_H */
