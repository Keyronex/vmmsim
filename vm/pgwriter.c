/*!
 * @file swapper.c
 * @brief The swapper trims working sets and writes modified anonymous pages.
 */

#include <sys/param.h>

#include <kdk/io.h>
#include <kdk/libkern.h>
#include <kdk/nanokern.h>
#include <unistd.h>
#include <fcntl.h>

#include "vmp.h"

#define MAX_CLUSTER 8
#define MAX_IOPS 32

typedef struct vmp_pagefile {
	vnode_t *vnode;
	uint8_t *bitmap;
	size_t total_slots;
	size_t free_slots;
	size_t next_free;
} vmp_pagefile_t;

kevent_t vmp_sufficient_pages_event;
kevent_t vmp_pgwriter_event;
vmp_pagefile_t vmp_pagefile;

int
init_vmp_pagefile(vmp_pagefile_t *pf, vnode_t *vnode, size_t length)
{
	if (!pf || !vnode)
		return -1;

	pf->vnode = vnode;
	pf->total_slots = length / PGSIZE;
	pf->free_slots = pf->total_slots;
	pf->next_free = 0;

	size_t bitmap_size = (pf->total_slots + 7) / 8;

	pf->bitmap = (uint8_t *)kmem_alloc(bitmap_size);
	if (!pf->bitmap)
		return -1;

	memset(pf->bitmap, 0, bitmap_size);

	return 0;
}

uintptr_t
vmp_pagefile_alloc(vmp_pagefile_t *pf)
{
	if (!pf || pf->free_slots == 0)
		return -1;

	size_t start = pf->next_free;
	for (size_t i = 0; i < pf->total_slots; ++i) {
		size_t idx = (start + i) % pf->total_slots;
		size_t byte_index = idx / 8;
		size_t bit_index = idx % 8;

		if (!(pf->bitmap[byte_index] & (1 << bit_index))) {
			pf->bitmap[byte_index] |= (1 << bit_index);
			pf->free_slots--;

			pf->next_free = (idx + 1) % pf->total_slots;
			return idx;
		}
	}

	return -1;
}

void
SIM_paging_init(void)
{
	static vnode_t vnode;

	vnode.fd = open("pagefile", O_RDWR);
	kassert(vnode.fd != -1);

	init_vmp_pagefile(&vmp_pagefile, &vnode, 1024 * 1024);
}

void *
vmp_pgwriter(void *)
{
	size_t n_to_clean;
	size_t n_iops;
	iop_t *iops = kmem_alloc(sizeof(iop_t) * MAX_IOPS);
	void **wait_events = kmem_alloc(sizeof(void *) * MAX_IOPS);
	vm_mdl_t **mdls = kmem_alloc(sizeof(vm_mdl_t *) * MAX_IOPS);

	for (int i = 0; i < MAX_IOPS; i++)
		vm_mdl_alloc(&mdls[i], MAX_CLUSTER);

	for (int i = 0; i < MAX_IOPS; i++)
		wait_events[i] = &iops[i].event;

loop:
	ke_event_wait(&vmp_pgwriter_event, NS_PER_S);

	printf("Pgwriter wakes\n");

	vm_dump_page_summary();

	if (vmp_page_shortage())
		n_to_clean = MAX(32, vmstat.nmodified / 30);
	else
		n_to_clean = MAX(16, vmstat.nmodified / 45);

	n_iops = 0;

	while (n_to_clean > 0 && n_iops < MAX_IOPS) {
		vm_page_t *page;
		ipl_t ipl;

		ipl = vmp_acquire_pfn_lock();
		page = TAILQ_FIRST(&modified_pgq);
		if (page == NULL) {
			ke_event_clear(&vmp_pgwriter_event);
			vmp_release_pfn_lock(ipl);
			break;
		}

		/* this removes it from the queue */
		vmp_page_retain_locked(page);

		/* TODO(feature): clustered writeback */
		switch (page->use) {
		case kPageUseAnonPrivate:
		case kPageUseAnonShared:
		case kPageUsePML4:
		case kPageUsePML3:
		case kPageUsePML2:
		case kPageUsePML1: {
			vm_mdl_t *mdl = mdls[n_iops];
			iop_t *iop = &iops[n_iops];

			if (page->drumslot == -1) {
				uintptr_t swapdesc;
				swapdesc = vmp_pagefile_alloc(&vmp_pagefile);
				page->drumslot = swapdesc;
			}

			mdl->offset = 0;
			mdl->nentries = 1;
			mdl->pages[0] = page;

			kprintf("Paging out %lu\n", page->pfn);
			iop_init_vnode_write(iop, vmp_pagefile.vnode, mdl,
			    PGSIZE, page->drumslot * PGSIZE);

			page->dirty = false;
			vmp_release_pfn_lock(ipl);

			iop_send(iop);
			n_iops++;

			break;
		}

		default:
			kfatal("Can't clean this page.\n");
		}
	}

	if (n_iops > 0) {
		for (int i = 0; i < n_iops; i++)
			ke_event_wait(wait_events[i], -1);

		for (int i = 0; i < n_iops; i++)
			vm_mdl_release_pages(mdls[i]);
	}

	goto loop;
}
