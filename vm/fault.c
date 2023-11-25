#include <kdk/executive.h>

#include "vm.h"
#include "vm/vmpsoft.h"
#include "vmp.h"

static int
do_fault(vaddr_t vaddr, bool write, vm_mdl_t *out)
{
	eprocess_t *ps = &kernel_ps;
	struct vmp_pte_wire_state pte_state;
	enum vmp_pte_kind pte_kind;
	vm_vad_t *vad;
	ipl_t ipl;
	int ret = 0;

	ke_wait(&ps->vad_lock, "vm_fault:ps->vad_lock", false, false, -1);

	vad = vmp_ps_vad_find(ps, vaddr);
	kassert(vad != NULL);

	ke_wait(&ps->ws_lock, "vm_fault:ps->ws_lock", false, false, -1);

	vmp_wire_pte(ps, vaddr, &pte_state);
	ipl = vmp_acquire_pfn_lock();
	pte_kind = vmp_pte_characterise(pte_state.pte);

	if (pte_kind == kPTEKindValid &&
	    !vmp_pte_hw_is_writeable(pte_state.pte) && write) {
		/*
		 * Write fault, VAD permits, PTE valid, PTE not writeable.
		 * Possibilities:
		 * - this page is legally writeable but is not set writeable
		 *   because of dirty-bit emulation.
		 * - this is a CoW page
		 */

		if (vad->flags.cow) {
			kfatal("cow section fault\n");
		} else if (false /* page is fork anon */) {
		} else {
			pte_state.pte->hw.writeable = true;
			if (out != NULL) {
				vm_page_t *page = vmp_pte_hw_page(pte_state.pte,
				    1);
				vmp_page_retain_locked(page);
				out->pages[out->offset / PGSIZE] = page;
				out->offset += PGSIZE;
			}
		}
	} else if (pte_kind == kPTEKindZero) {
		if (vad->section == NULL) {
			/*! demand paged zero */

			vm_page_t *page;
			int r;

			r = vmp_page_alloc_locked(&page, kPageUseAnonPrivate,
			    false);
			if (r != 0) {
				ret = r;
				goto out;
			}

			vmp_pte_hw_create(pte_state.pte, page->pfn,
			    write & vad->flags.writeable);
			vmp_pagetable_page_nonswap_pte_created(ps,
			    pte_state.pages[0], true);
			vmp_wsl_insert(ps, vaddr, false);
			page->referent_pte = V2P(pte_state.pte);

			if (out != NULL) {
				vmp_page_retain_locked(page);
				out->pages[out->offset / PGSIZE] = page;
				out->offset += PGSIZE;
			}
		} else {
			kfatal("Do file read fault\n");
		}
	} else if (pte_kind == kPTEKindTrans) {
		vm_page_t *page = vmp_pte_trans_page(pte_state.pte);
		vmp_page_retain_locked(page);
		vmp_pte_hw_create(pte_state.pte, page->pfn, false);
		vmp_wsl_insert(ps, vaddr, false);
		if (out != NULL && !write) {
			vmp_page_retain_locked(page);
			out->pages[out->offset / PGSIZE] = page;
			out->offset += PGSIZE;
		}
	} else if (pte_kind == kPTEKindValid) {
		if (out != NULL) {
			vm_page_t *page = vmp_pte_hw_page(pte_state.pte, 1);
			vmp_page_retain_locked(page);
			out->pages[out->offset / PGSIZE] = page;
			out->offset += PGSIZE;
		}
	} else {
		kfatal("Unhandled PTE kind %d\n", pte_kind);
	}

out:
	vmp_pte_wire_state_release(&pte_state);
	vmp_release_pfn_lock(ipl);
	ke_mutex_release(&ps->ws_lock);
	ke_mutex_release(&ps->vad_lock);

	return ret;
}

int
vm_fault(vaddr_t vaddr, bool write, vm_mdl_t *out)
{
	int r;

retry:
	r = do_fault(vaddr, write, out);
	switch (r) {
	case 0:
		return 0;

	case -1:
		ke_event_wait(&vmp_sufficient_pages_event, -1);
		goto retry;

	default:
		kfatal("Unexpected return value from do_fault\n");
	}
}
