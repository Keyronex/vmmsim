#include <kdk/executive.h>

#include "io.h"
#include "nanokern.h"
#include "vm.h"
#include "vm/vmpsoft.h"
#include "vmp.h"

struct vmp_pager_state *
vmp_pager_state_alloc()
{
	vmp_pager_state_t *state = kmem_alloc(sizeof(*state));
	if (state == NULL)
		return NULL;
	state->refcount = 1;
	ke_event_init(&state->event, false);
	return state;
}

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

			page->process = ps;
			vmp_pte_hw_create(pte_state.pte, page->pfn,
			    write & vad->flags.writeable);
			vmp_pagetable_page_nonswap_pte_created(ps,
			    pte_state.pages[0], true);
			vmp_wsl_insert(ps, vaddr, false, false);
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
		vmp_wsl_insert(ps, vaddr, false, false);
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
	} else if (pte_kind == kPTEKindSwap) {

		struct vmp_pager_state *pager_state;
		vm_mdl_t *mdl;
		vm_page_t *page;
		iop_t iop;
		int r;

		r = vmp_page_alloc_locked(&page, kPageUseAnonPrivate, false);
		if (r != 0) {
			ret = r;
			goto out;
		}
		page->process = ps;
		page->referent_pte = V2P(pte_state.pte);

		pager_state = vmp_pager_state_alloc();
		vm_mdl_alloc(&mdl, 1);
		kassert(pager_state != NULL);
		kassert(mdl != NULL);

		vmp_pte_busy_create(pte_state.pte, pager_state);
		vmp_pagetable_page_nonswap_pte_created(ps, pte_state.pages[0],
		    false);
		vmp_wsl_insert(ps, vaddr, false, true);

		vmp_pte_wire_state_release(&pte_state);
		vmp_release_pfn_lock(ipl);
		ke_mutex_release(&ps->ws_lock);
		ke_mutex_release(&ps->vad_lock);

		mdl->offset = 0;
		mdl->nentries = 1;
		mdl->pages[0] = page;

		ke_event_init(&iop.event, false);
		iop_init_vnode_read(&iop, vmp_pagefile.vnode, mdl, PGSIZE,
		    page->drumslot * PGSIZE);
		iop_send(&iop);

		ke_event_wait(&iop.event, -1);

		ke_wait(&ps->vad_lock, "ps->vad_lock reacquire swapin", false,
		    false, -1);
		ke_wait(&ps->ws_lock, "ps->ws_lock reacquire swapin", false,
		    false, -1);
		vmp_acquire_pfn_lock();

		if (out != NULL) {
			vmp_page_retain_locked(page);
			out->pages[out->offset / PGSIZE] = page;
			out->offset += PGSIZE;
		}

		vmp_pte_hw_create(pte_state.pte, page->pfn, false);
		vmp_wsl_unlock_entry(ps, vaddr);

		goto out_no_pte_wire_state_release;
	} else {
		kfatal("Unhandled PTE kind %d\n", pte_kind);
	}

out:
	vmp_pte_wire_state_release(&pte_state);
out_no_pte_wire_state_release:
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
