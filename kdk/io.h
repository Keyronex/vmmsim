#ifndef KRX_KDK_IO_H
#define KRX_KDK_IO_H

#include <kdk/nanokern.h>

typedef struct vnode {
	int fd;
} vnode_t;

typedef struct iop {
	bool is_write;
	vnode_t *vnode;
	struct vm_mdl *mdl;
	kevent_t event;
	size_t length;
	size_t offset;
} iop_t;

void iop_init_vnode_read(iop_t *iop, struct vnode *vnode, struct vm_mdl *mdl,
    size_t length, size_t off);
void iop_init_vnode_write(iop_t *iop, struct vnode *vnode, struct vm_mdl *mdl,
    size_t length, size_t off);
void iop_send(iop_t *iop);

#endif /* KRX_KDK_IO_H */
