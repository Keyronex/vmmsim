#include <kdk/executive.h>

#include "vmp.h"

struct vmp_wsle {
	TAILQ_ENTRY(vmp_wsle) queue_entry;
	RB_ENTRY(vmp_wsle) rb_entry;
	vaddr_t vaddr;
};

static inline intptr_t
wsle_cmp(struct vmp_wsle *x, struct vmp_wsle *y)
{
	return x->vaddr - y->vaddr;
}

RB_GENERATE(vmp_wsle_rb, vmp_wsle, rb_entry, wsle_cmp);

static struct vmp_wsle *
vmp_wsl_find(eprocess_t *ps, vaddr_t vaddr)
{
	struct vmp_wsle key;
	key.vaddr = vaddr;
	return RB_FIND(vmp_wsle_rb, &ps->wsl.tree, &key);
}

void
vmp_wsl_insert(eprocess_t *ps, vaddr_t vaddr, bool locked)
{
	struct vmp_wsle *wsle;

	kassert(vmp_wsl_find(ps, vaddr) == NULL);

	if (locked)
		ps->wsl.nlocked++;

	wsle = kmem_alloc(sizeof(*wsle));
	wsle->vaddr = vaddr;
	if (!locked)
		TAILQ_INSERT_TAIL(&ps->wsl.queue, wsle, queue_entry);
	RB_INSERT(vmp_wsle_rb, &ps->wsl.tree, wsle);
}

void
vmp_wsl_remove(eprocess_t *ps, vaddr_t vaddr)
{
	struct vmp_wsle *wsle = vmp_wsl_find(ps, vaddr);
	kassert(wsle != NULL);
	RB_REMOVE(vmp_wsle_rb, &ps->wsl.tree, wsle);
	TAILQ_REMOVE(&ps->wsl.queue, wsle, queue_entry);
	kmem_free(wsle, sizeof(*wsle));
}

void
vmp_wsl_lock_entry(eprocess_t *ps, vaddr_t vaddr)
{
	struct vmp_wsle *wsle = vmp_wsl_find(ps, vaddr);
	kassert(wsle != NULL);
	TAILQ_REMOVE(&ps->wsl.queue, wsle, queue_entry);
	ps->wsl.nlocked++;
}

void
vmp_wsl_unlock_entry(eprocess_t *ps, vaddr_t vaddr)
{
	struct vmp_wsle *wsle = vmp_wsl_find(ps, vaddr);
	kassert(wsle != NULL);
	TAILQ_INSERT_TAIL(&ps->wsl.queue, wsle, queue_entry);
	ps->wsl.nlocked--;
}

void
vmp_wsl_dump(eprocess_t *ps)
{
	struct vmp_wsle *wsle;
	kprintf("WSL: %zu locked:\n", ps->wsl.nlocked);
	kprintf("All entries:\n");
	RB_FOREACH (wsle, vmp_wsle_rb, &ps->wsl.tree) {
		kprintf("0x%zx\n", wsle->vaddr);
	}
}
