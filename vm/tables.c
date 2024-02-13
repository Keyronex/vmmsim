#include <kdk/executive.h>
#include <string.h>

#include "vmp.h"

static bool
page_is_root_table(vm_page_t *page)
{
	return page->use == (kPageUsePML1 + (VMP_TABLE_LEVELS - 1));
}

void
vmp_pagetable_page_nonswap_pte_created(eprocess_t *ps, vm_page_t *page,
    bool is_new)
{
	vmp_page_retain_locked(page);
	if (is_new)
		page->nonzero_ptes++;
	if (page->nonswap_ptes++ == 0 && !page_is_root_table(page)) {
		vmp_wsl_lock_entry(ps, P2V(vmp_page_paddr(page)));
	}
}

void
vmp_pagetable_page_pte_became_swap(eprocess_t *ps, vm_page_t *page)
{
	if (page->nonswap_ptes-- == 1 && !page_is_root_table(page))
		vmp_wsl_unlock_entry(ps, P2V(vmp_page_paddr(page)));
	vmp_page_release_locked(page);
}

static void vmp_md_delete_table_pointers(struct eprocess *ps,
    vm_page_t *dirpage, pte_t *dirpte);

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
	if (page->nonzero_ptes-- == 1 && !page_is_root_table(page)) {
		vm_page_t *dirpage;

		page->use = kPageUseDeleted;

		if (page->nonswap_ptes == 1) {
			vmp_wsl_unlock_entry(ps, P2V(vmp_page_paddr(page)));
			vmp_wsl_remove(ps, P2V(vmp_page_paddr(page)));
		} else if (page->nonswap_ptes == 0)
			vmp_wsl_remove(ps, P2V(vmp_page_paddr(page)));
		else
			kfatal("expectex nonswap_ptes to be 0 or 1\n");

		dirpage = vmp_paddr_to_page(page->referent_pte);
		vmp_md_delete_table_pointers(ps, dirpage,
		    (pte_t *)P2V(page->referent_pte));

		page->nonswap_ptes = 0;
		page->referent_pte = 0;

		/*! once for the working set removal.... */
		vmp_page_release_locked(page);
		/*! and once for the PTE zeroing; this will free the page. */
		vmp_page_release_locked(page);

		return;
	}
	if (!was_swap && page->nonswap_ptes-- == 1 &&
	    !page_is_root_table(page)) {
		vmp_wsl_unlock_entry(ps, P2V(vmp_page_paddr(page)));
	}
	vmp_page_release_locked(page);
}

/*! @brief Convert the PTEs pointing to page table \p dirpage to trans PTEs. */
void
vmp_md_transition_table_pointers(struct eprocess *ps, vm_page_t *dirpage,
    vm_page_t *tablepage)
{
	kfatal("Implement me!\n");
}

static void
vmp_md_delete_table_pointers(struct eprocess *ps, vm_page_t *dirpage,
    pte_t *dirpte)
{
	kassert(vmp_pte_characterise(dirpte) == kPTEKindValid ||
	    vmp_pte_characterise(dirpte) == kPTEKindTrans);
	vmp_pte_zero_create(dirpte);
	vmp_pagetable_page_pte_deleted(ps, dirpage, false);
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
		if (state->pages[i] == NULL)
			continue;
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
			return 0;
		}

#if DEBUG_TABLES
		printf("Dealing with pte in level %d; pte is %p; page is %p\n",
		    level, pte, pages[level - 1]);
#endif

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
			page->nonzero_ptes++;
			page->nonswap_ptes++;
			vmp_wsl_insert(ps, P2V(next_table_p), true, true);

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

			/* manually adjust the new page */
			vmp_page_retain_locked(page);
			page->process = ps;
			page->nonzero_ptes++;
			page->nonswap_ptes++;
			page->referent_pte = V2P(pte);
			vmp_wsl_insert(ps, P2V(vmp_page_paddr(page)), true, true);

			vmp_md_setup_table_pointers(ps, pages[level - 1], page,
			    pte, true);

			table = (pte_t *)P2V(vmp_pte_hw_paddr(pte, level));
			break;
		}
		}
	}
	kfatal("unreached\n");
}

int
vmp_fetch_pte(eprocess_t *ps, vaddr_t vaddr, pte_t **pte_out)
{
	ipl_t ipl;
	int indexes[VMP_TABLE_LEVELS + 1];
	pte_t *table;

	vmp_addr_unpack(vaddr, indexes);

	table = ps->pml4;

	for (int level = VMP_TABLE_LEVELS; level > 0; level--) {
		pte_t *pte = &table[indexes[level]];

		/* note - level is 1-based */

		if (level == 1) {
			*pte_out = pte;
			return 0;
		}

		if (vmp_pte_characterise(pte) != kPTEKindValid)
			return -1;

		table = (pte_t *)P2V(vmp_pte_hw_paddr(pte, level));
	}
	kfatal("unreached\n");
}

int
vmp_fork(eprocess_t *ps1, eprocess_t *ps2)
{
	pte_t *l4table = ps1->pml4;
	int indexes[VMP_TABLE_LEVELS + 1];
	vm_page_t *pages[VMP_TABLE_LEVELS] = { 0 };

	ke_wait(&ps1->vad_lock, "vmp_fork:ps1->vad_lock", false, false, -1);
	ke_wait(&ps2->vad_lock, "vmp_fork:ps1->vad_lock", false, false, -1);

#if VMP_TABLE_LEVELS >= 4
	for (int i4 = 0; i4 < VMP_LEVEL_4_ENTRIES; i4 += VMP_LEVEL_4_STEP) {
		pte_t *l4pte = &l4table[i4];
#endif

#if VMP_TABLE_LEVELS >= 3
		for (int i3 = 0; i3 < VMP_LEVEL_3_ENTRIES;
		     i3 += VMP_LEVEL_3_STEP) {
#endif
#if VMP_TABLE_LEVELS >= 2
			for (int i2 = 0; i2 < VMP_LEVEL_2_ENTRIES;
			     i2 += VMP_LEVEL_2_STEP) {
#endif
				for (int i1 = 0; i1 < VMP_LEVEL_1_ENTRIES;
				     i1 += VMP_LEVEL_1_STEP) {
				}
#if VMP_TABLE_LEVELS >= 2
			}
#endif
#if VMP_TABLE_LEVELS >= 3
		}
#endif
#if VMP_TABLE_LEVELS >= 4
	}
#endif

	ke_mutex_release(&ps2->vad_lock);
	ke_mutex_release(&ps1->vad_lock);
}
