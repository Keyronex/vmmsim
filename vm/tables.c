#include <kdk/executive.h>
#include <string.h>

#include "vm.h"
#include "vm/vmpsoft.h"
#include "vmp.h"

static bool
page_is_root_table(vm_page_t *page)
{
	return page->use == (kPageUsePML1 + (VMP_TABLE_LEVELS - 1));
}

/*!
 * @brief Update pagetable page after nonswap PTE(s) created within it.
 *
 * This will amend the PFNDB entry's nonswap PTE count, and if the previous
 * nonswap PTE count was 0, lock the page into the working set.
 *
 * @param is_new Whether the nonswap PTE is brand new (replacing a zero PTE; if
 * so, used_ptes count must be increased as well as nonswap_ptes.)
 *
 * @pre ps->ws_lock held
 */
static void
vmp_pagetable_page_nonswap_pte_created(eprocess_t *ps, vm_page_t *page,
    bool is_new)
{
	vmp_page_retain_locked(page);
	if (is_new)
		page->used_ptes++;
	if (page->nonswap_ptes++ == 0 && !page_is_root_table(page)) {
		vmp_wsl_lock_entry(ps, P2V(vmp_page_paddr(page)));
	}
}

/*!
 * @brief Update pagetable page after PTE(s) made zero within it.
 *
 * This will amend the PFNDB entry's nonswap and nonzero PTE count, and if the
 * new nonzero PTE count is zero, delete the page. If the new nonswap PTE count
 * is zero, the page will be unlocked from its owning process' working set.
 *
 * @pre ps->ws_lock held
 */
static void
vmp_pagetable_page_pte_deleted(struct eprocess *ps, vm_page_t *page,
    bool was_swap)
{
	if (page->used_ptes-- == 1) {
		kfatal("delete this page\n");
		return;
	}
	if (!was_swap && page->nonswap_ptes-- == 1 &&
	    !page_is_root_table(page)) {
		vmp_wsl_unlock_entry(ps, P2V(vmp_page_paddr(page)));
	}
	vmp_page_release_locked(page);
}

static void
vmp_md_setup_table_pointers(struct eprocess *ps, vm_page_t *dirpage,
    vm_page_t *tablepage, pte_t *dirpte, bool is_new)
{
	vmp_pagetable_page_nonswap_pte_created(ps, dirpage, is_new);
	vmp_pte_hw_create(dirpte, tablepage->pfn, true);
}

static void
vmp_pager_state_release(vmp_pager_state_t *state)
{
}

void
vmp_pte_wire_state_release(struct vmp_pte_wire_state *state)
{
	for (int i = 0; i < VMP_TABLE_LEVELS; i++) {
		vmp_pagetable_page_pte_deleted(state->ps, state->pages[i],
		    false);
	}
}

/*!
 * Note: WS lock and PFN lock will be locked and unlocked regularly here.
 * \pre VAD list mutex held
 */
int
vmp_wire_pte(eprocess_t *ps, vaddr_t vaddr, struct vmp_pte_wire_state *state)
{
	ipl_t ipl;
	int indexes[VMP_TABLE_LEVELS + 1];
	vm_page_t *pages[VMP_TABLE_LEVELS] = { 0 };
	pte_t *table;

	vmp_addr_unpack(vaddr, indexes);
	state->ps = ps;

	ke_wait(&ps->ws_lock, "vmp_wire_pte:ws_lock", false, false, -1);
	ipl = vmp_acquire_pfn_lock();

	/*
	 * start by pinning root table with a valid-pte reference, to keep it
	 * locked in the working set. this same approach is used through the
	 * function.
	 *
	 * the principle is that at each iteration, the page table we are
	 * examining has been locked into the working set by the processing of
	 * the prior level. as such, pin the root table by calling the
	 * new-nonswap-pte function; this pins the page.
	 */
	table = ps->pml4;
	pages[VMP_TABLE_LEVELS - 1] = ps->pml4_page;
	vmp_pagetable_page_nonswap_pte_created(ps, ps->pml4_page, true);

	for (int level = VMP_TABLE_LEVELS; level > 0; level--) {
		pte_t *pte = &table[indexes[level]];

		/* note - level is 1-based */

		if (level == 1) {
			memcpy(state->pages, pages, sizeof(pages));
			state->pte = pte;
			vmp_release_pfn_lock(ipl);
			ke_mutex_release(&ps->ws_lock);
			return 0;
		}

		printf("Dealing with entry in level %d\n", level);

	restart_level:
		switch (vmp_pte_characterise(pte)) {
		case kPTEKindValid: {
			vm_page_t *page = vmp_pte_hw_page(pte, level);
			pages[level - 2] = page;
			vmp_pagetable_page_nonswap_pte_created(ps, page, true);
			table = (pte_t *)P2V(vmp_pte_hw_paddr(pte, level));
			break;
		}

		case kPTEKindTrans: {
			paddr_t next_table_p = vmp_pfn_to_paddr(pte->trans.pfn);
			vm_page_t *page = vmp_paddr_to_page(next_table_p);
			/* retain for our wiring purposes */
			pages[level - 2] = vmp_page_retain_locked(page);

			/* manually adjust the page */
			vmp_page_retain_locked(page);
			page->used_ptes++;
			page->nonswap_ptes++;
			vmp_wsl_insert(ps, P2V(next_table_p), true);

			vmp_md_setup_table_pointers(ps, pages[level - 1], page,
			    pte, false);

			table = (pte_t *)P2V(vmp_pte_hw_paddr(pte, level));
			break;
		}

		case kPTEKindBusy: {
			vm_page_t *page = vmp_pte_hw_page(pte, level);
			vmp_pager_state_t *state = page->pager_state;
			state->refcount++;
			vmp_release_pfn_lock(ipl);
			ke_mutex_release(&ps->ws_lock);
			ke_event_wait(&state->event, -1);
			vmp_pager_state_release(state);
			ke_wait(&ps->ws_lock, "vmp_wire_pte: reacquire ws_lock",
			    false, false, -1);
			ipl = vmp_acquire_pfn_lock();
			goto restart_level;
		}

		case kPTEKindSwap:
			kfatal("swap page back in\n");
			break;

		case kPTEKindZero: {
			vm_page_t *page;
			int r;

			/* newly-allocated page is retained */
			r = vmp_page_alloc_locked(&page,
			    kPageUsePML1 + (level - 2), false);
			kassert(r == 0);

			pages[level - 2] = page;

			/* manually adjust the page */
			vmp_page_retain_locked(page);
			page->used_ptes++;
			page->nonswap_ptes++;
			vmp_wsl_insert(ps, P2V(vmp_page_paddr(page)), true);

			vmp_md_setup_table_pointers(ps, pages[level - 1], page,
			    pte, true);

			table = (pte_t *)P2V(vmp_pte_hw_paddr(pte, level));
			break;
		}
		}
	}
	kfatal("unreached\n");
}
