#include <string.h>

#include "vm/vmp.h"

uint8_t SOFT_pages[256 * 4096];
static vm_page_t mypages[256];
kspinlock_t vmp_pfn_lock = KSPINLOCK_INITIALISER;

struct vm_stat {
	size_t nfree, nmodified, nstandby, nactive;
} vmstat;

vmp_page_queue_t free_pgq = TAILQ_HEAD_INITIALIZER(free_pgq),
		 standby_pgq = TAILQ_HEAD_INITIALIZER(standby_pgq),
		 modified_pgq = TAILQ_HEAD_INITIALIZER(modified_pgq);

void
SIM_pages_init(void)
{
	for (int i = 0; i < 256; i++) {
		mypages[i].pfn = i;
		mypages[i].referent_pte = 0;
		mypages[i].used_ptes = 0;
		mypages[i].refcnt = 0;
		mypages[i].use = kPageUseFree;
		TAILQ_INSERT_TAIL(&free_pgq, &mypages[i], queue_link);
	}
	vmstat.nfree = 256;
}

int
vmp_page_alloc_locked(vm_page_t **out, enum vm_page_use use, bool must)
{
	vm_page_t *page;

	kassert(ke_spinlock_held(&vmp_pfn_lock));

	page = TAILQ_FIRST(&free_pgq);
	kassert(page != NULL);
	TAILQ_REMOVE(&free_pgq, page, queue_link);

	kassert(page->refcnt == 0);
	kassert(page->used_ptes == 0);
	kassert(page->referent_pte == 0);
	page->refcnt = 1;
	page->use = use;
	page->dirty = false;

	vmstat.nfree--;
	vmstat.nactive++;

	*out = page;

	memset((void *)vm_page_direct_map_addr(page), 0x0, PGSIZE);

	return 0;
}

vm_page_t *
vmp_page_retain_locked(vm_page_t *page)
{
	if (page->refcnt++ == 0) {
		/* going from inactive to active state */
		kassert(page->use != kPageUseDeleted);
		if (page->dirty) {
			TAILQ_REMOVE(&modified_pgq, page, queue_link);
			vmstat.nmodified -= 1;
			vmstat.nactive += 1;
		} else {
			TAILQ_REMOVE(&standby_pgq, page, queue_link);
			vmstat.nstandby -= 1;
			vmstat.nactive += 1;
		}
	}

	return page;
}

void
vmp_page_release_locked(vm_page_t *page)
{
	kassert(page->refcnt > 0);

	if (page->refcnt-- == 1) {
		/* going from active to inactive state */
		vmstat.nactive -= 1;

		switch (page->use) {
		default:
			kfatal("Release page of unexpected type\n");
		}

		/* this is a pageable page, so put it on the appropriate q */

		if (page->dirty) {
			TAILQ_INSERT_TAIL(&modified_pgq, page, queue_link);
			vmstat.nmodified += 1;
		} else {
			TAILQ_INSERT_TAIL(&standby_pgq, page, queue_link);
			vmstat.nstandby += 1;
		}
	}
}

vm_page_t *
vmp_paddr_to_page(paddr_t paddr)
{
	return &mypages[paddr / PGSIZE];
}

static const char *
vm_page_use_str(enum vm_page_use use)
{
	switch (use) {
	case kPageUseFree:
		return "free";
	case kPageUseAnonPrivate:
		return "anon-private";
	case kPageUsePML4:
		return "PML4";
	case kPageUsePML3:
		return "PML3";
	case kPageUsePML2:
		return "PML2";
	case kPageUsePML1:
		return "PML1";
	default:
		return "BAD";
	}
}

void
vm_dump_pages(void)
{
	for (int i = 0; i < 256; i++) {
		vm_page_t *page = &mypages[i];
		if (mypages[i].use == kPageUseFree)
			continue;
		printf("PFN %d: Use %s RC %d Used-PTE %d Valid-PTE %d\n", i,
		    vm_page_use_str(page->use), page->refcnt, page->used_ptes,
		    page->nonswap_ptes);
	}
}
