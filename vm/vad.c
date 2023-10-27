#include "executive.h"
#include "vmp.h"

int vmp_vad_cmp(vm_vad_t *x, vm_vad_t *y);

RB_GENERATE(vm_vad_rbtree, vm_vad, rb_entry, vmp_vad_cmp);

int
vmp_vad_cmp(vm_vad_t *x, vm_vad_t *y)
{
	/*
	 * what this actually does is determine whether x's start address is
	 * lower than, greater than, or within the bounds of Y. it works because
	 * we allocate virtual address space with vmem, which already ensures
	 * there are no overlaps.
	 */

	if (x->start < y->start)
		return -1;
	else if (x->start >= y->end)
		return 1;
	else
		/* x->start is within VAD y */
		return 0;
}

vm_vad_t *
vmp_ps_vad_find(eprocess_t *ps, vaddr_t vaddr)
{
	vm_vad_t key;
	key.start = vaddr;
	return RB_FIND(vm_vad_rbtree, &ps->vad_tree, &key);
}

int
vm_ps_allocate(eprocess_t *ps, vaddr_t *vaddrp, size_t size, bool exact)
{
	return vm_ps_map_section_view(ps, NULL, vaddrp, size, 0, true, true,
	    false, false, exact);
}

int
vm_ps_map_section_view(eprocess_t *ps, void *section, vaddr_t *vaddrp,
    size_t size, uint64_t offset, bool initial_writeability,
    bool max_writeability, bool inherit_shared, bool cow, bool exact)
{
	int r;
	kwaitstatus_t w;
	vm_vad_t *vad;
	vaddr_t addr = exact ? *vaddrp : 0;

	ke_wait(&ps->vad_lock, "map_section_view:ps->vad_lock", false, false,
	    -1);

	vad = kmem_alloc(sizeof(vm_vad_t));
	vad->start = (vaddr_t)addr;
	vad->end = addr + size;
	vad->flags.cow = cow;
	vad->flags.offset = offset;
	vad->flags.inherit_shared = inherit_shared;
	vad->flags.writeable = initial_writeability;
	vad->flags.max_protection = max_writeability;
	vad->section = section;

	RB_INSERT(vm_vad_rbtree, &ps->vad_tree, vad);

	ke_mutex_release(&ps->vad_lock);

	*vaddrp = addr;

	return 0;
}
