#include <kdk/io.h>
#include <kdk/nanokern.h>
#include <kdk/vm.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "vm/vmp.h"

void iop_init_vnode_read(iop_t *iop, struct vnode *vnode, struct vm_mdl *mdl,
    size_t length, size_t off)
{
	iop->vnode = vnode;
	iop->mdl = mdl;
	iop->length = length;
	iop->offset = off;
	iop->is_write = false;
	ke_event_clear(&iop->event);
}

void iop_init_vnode_write(iop_t *iop, struct vnode *vnode, struct vm_mdl *mdl,
    size_t length, size_t off)
{
	iop->vnode = vnode;
	iop->mdl = mdl;
	iop->length = length;
	iop->offset = off;
	iop->is_write = true;
	ke_event_clear(&iop->event);
}

void iop_send(iop_t *iop)
{
	kassert(iop->vnode && iop->vnode->fd != -1);
	for (int i =0; i < iop->mdl->nentries; i++) {
		void *data = (void*)P2V(vmp_page_paddr(iop->mdl->pages[i]));
		size_t offset = iop->offset + i * PGSIZE;
		if (iop->is_write)
			pwrite(iop->vnode->fd, data, PGSIZE, offset);
		else
			pread(iop->vnode->fd, data, PGSIZE, offset);
	}
	ke_event_signal(&iop->event);
}
