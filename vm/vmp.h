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

struct vm_stat {
	size_t nfree, nmodified, nstandby, nactive;
	size_t ntotal;
};

struct vm_param {
	/*! count of pages to expand a working set by */
	size_t ws_page_expansion_count;
	/*! minimum available pages for WS expansion */
	size_t min_avail_for_expansion;
	/*! minimum available pages for regular allocations */
	size_t min_avail_for_alloc;
};

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

typedef struct vm_vad {
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
} vm_vad_t;

int vmp_page_alloc_locked(vm_page_t **out, enum vm_page_use use, bool must);
vm_page_t *vmp_page_retain_locked(vm_page_t *page);
void vmp_page_release_locked(vm_page_t *page);
vm_page_t *vmp_paddr_to_page(paddr_t paddr);

/*!
 * @brief Insert one entry into a working set list.
 *
 * n.b. Page should be REFERENCED - this effectively consumes that reference.
 *
 * @pre PFNDB lock held
 * @pre WS lock held
 */
void vmp_wsl_insert(struct eprocess *ps, vaddr_t vaddr, bool locked)
    LOCK_REQUIRES(ps->ws_lock) LOCK_REQUIRES(pfn_lock);
/*!
 * @brief Remove one entry from a working set list.
 *
 * @pre PFNDB lock held
 * @pre WS lock held
 */
void vmp_wsl_remove(struct eprocess *ps, vaddr_t vaddr)
    LOCK_REQUIRES(ps->ws_lock) LOCK_REQUIRES(pfn_lock);
/*!
 * @brief Lock an existing entry into a working set list.
 * @pre WS lock held.
 */
void vmp_wsl_lock_entry(struct eprocess *ps, vaddr_t vaddr)
    LOCK_REQUIRES(ps->ws_lock);
/*!
 * @brief Unlock a locked entry from a working set list.
 * @pre WS lock held.
 */
void vmp_wsl_unlock_entry(struct eprocess *ps, vaddr_t vaddr)
    LOCK_REQUIRES(ps->ws_lock);

/*!
 * @brief Wire a PTE.
 * @pre WS lock held. (May be dropped and reacquired!)
 */
int vmp_wire_pte(struct eprocess *, vaddr_t, struct vmp_pte_wire_state *);
/*!
 * @brief Release locked PTE state.
 */
void vmp_pte_wire_state_release(struct vmp_pte_wire_state *);
/*!
 * @brief Get pointer to an in-memory PTE.
 * n.b. does not wire anything, should only be called when the PTE is stable
 * (due to being kernel wired memory or otherwise certain to be in-memory, etc.)
 */
int vmp_fetch_pte(struct eprocess *ps, vaddr_t vaddr, pte_t **pte_out);

/*!
 * @brief Update pagetable page after nonswap PTE(s) created within it.
 *
 * This will amend the PFNDB entry's nonswap PTE count, and if the previous
 * nonswap PTE count was 0, lock the page into the working set.
 *
 * @param is_new Whether the nonswap PTE is brand new (replacing a zero PTE; if
 * so, used_ptes count must be increased as well as nonswap_ptes.)
 */
void vmp_pagetable_page_nonswap_pte_created(struct eprocess *ps,
    vm_page_t *page, bool is_new) LOCK_REQUIRES(ps->ws_lock)
    LOCK_REQUIRES(pfn_lock);

vm_vad_t *vmp_ps_vad_find(struct eprocess *ps, vaddr_t vaddr);
int vm_ps_allocate(struct eprocess *ps, vaddr_t *vaddrp, size_t size,
    bool exact);
int vm_ps_map_section_view(struct eprocess *ps, void *section, vaddr_t *vaddrp,
    size_t size, uint64_t offset, bool initial_writeability,
    bool max_writeability, bool inherit_shared, bool cow, bool exact);

/* paddr_t vmp_page_paddr(vm_page_t *page) */
#define vmp_page_paddr(PAGE) ((paddr_t)(PAGE)->pfn << VMP_PAGE_SHIFT)

/* paddr_t vmp_pfn_to_paddr(pfn_t pfn) */
#define vmp_pfn_to_paddr(PFN) ((paddr_t)PFN << VMP_PAGE_SHIFT)

/* paddr_t vmp_pte_hw_paddr(pte_t *pte, int level) */
#define vmp_pte_hw_paddr(PTE, LVL) vmp_pfn_to_paddr(vmp_pte_hw_pfn(PTE, LVL))

/* vm_page_t *vmp_pte_hw_page(pte_t *pte, int level) */
#define vmp_pte_hw_page(PTE, LVL) vmp_paddr_to_page(vmp_pte_hw_paddr(PTE, LVL))

/* paddr_t vmp_pfn_to_paddr(pte_t *pte) */
#define vmp_pte_trans_paddr(PTE) vmp_pfn_to_paddr((PTE)->trans.pfn)

/* vm_page_t *vmp_pte_trans_page(pte_t *pte) */
#define vmp_pte_trans_page(PTE) vmp_paddr_to_page(vmp_pte_trans_paddr(PTE))

/* ipl_t vmp_acquire_pfn_lock(void) */
#define vmp_acquire_pfn_lock() ke_spinlock_acquire(&vmp_pfn_lock);

/* void vmp_release_pfn_lock(ipl_t ipl) */
#define vmp_release_pfn_lock(IPL) ke_spinlock_release(&vmp_pfn_lock, IPL)

/* bool vmp_page_shortage(void) LOCK_REQUIRES(vmp_pfn_lock) */
#define vmp_page_shortage() \
	((vmstat.nfree + vmstat.nstandby) < vmparam.min_avail_for_alloc)

extern struct vm_param vmparam;
extern struct vm_stat vmstat;
extern kspinlock_t vmp_pfn_lock;
extern kevent_t vmp_sufficient_pages_event;
extern kevent_t vmp_pgwriter_event;
extern vmp_page_queue_t free_pgq, standby_pgq, modified_pgq;

#endif /* KRX_VM_VMP_H */
