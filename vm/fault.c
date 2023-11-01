#include <kdk/executive.h>

#include "vm/vmpsoft.h"
#include "vmp.h"

int
vm_fault(vaddr_t vaddr, bool write, vm_mdl_t *out)
{
	eprocess_t *ps = &kernel_ps;
	struct vmp_pte_wire_state pte_state;
	enum vmp_pte_kind pte_kind;
	vm_vad_t *vad;
	ipl_t ipl;

	ke_wait(&ps->vad_lock, "vm_fault:ps->vad_lock", false, false, -1);

	vad = vmp_ps_vad_find(ps, vaddr);
	kassert(vad != NULL);

	ke_wait(&ps->ws_lock, "vm_fault:ps->ws_lock", false, false, -1);

	vmp_wire_pte(ps, vaddr, &pte_state);
	ipl = vmp_acquire_pfn_lock();
	pte_kind = vmp_pte_characterise(pte_state.pte);

	if (pte_kind == kPTEKindZero) {
		if (vad->section == NULL) {
			/*! demand paged zero */

			vm_page_t *page;
			int ret;

			ret = vmp_page_alloc_locked(&page, kPageUseAnonPrivate,
			    false);
			kassert(ret == 0);

			vmp_pte_hw_create(pte_state.pte, page->pfn,
			    write & vad->flags.writeable);
			vmp_pagetable_page_nonswap_pte_created(ps,
			    pte_state.pages[0], true);
			vmp_wsl_insert(ps, vaddr, false);

			if (out != NULL) {
				vmp_page_retain_locked(page);
				out->pages[out->offset / PGSIZE] = page;
				out->offset += PGSIZE;
			}
		} else {
			kfatal("Do file read fault\n");
		}
	} else {
		kfatal("Unhandled PTE kind\n");
	}

	vmp_pte_wire_state_release(&pte_state);
	vmp_release_pfn_lock(ipl);
	ke_mutex_release(&ps->ws_lock);
	ke_mutex_release(&ps->vad_lock);

	return 0;
}
