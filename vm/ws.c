#include <kdk/executive.h>

#include "defs.h"
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

static void
wsl_evict(eprocess_t *ps, pte_t *pte)
{
	bool dirty = vmp_pte_hw_is_writeable(pte);
	vm_page_t *page = vmp_pte_hw_page(pte, 1);

	page->dirty |= dirty;

	switch (page->use) {
	case kPageUseAnonPrivate: {
		vmp_pte_trans_create(pte, vmp_pte_hw_pfn(pte, 1));
		break;
	}

	default:
		kfatal("Implement me\n");
	}

	vmp_page_release_locked(page);
}

static struct vmp_wsle *
wsl_trim_1(eprocess_t *ps)
{
	struct vmp_wsle *wsle;
	pte_t *pte;
	int r;

	wsle = TAILQ_FIRST(&ps->wsl.queue);
	if (wsle == NULL)
		return NULL;

	TAILQ_REMOVE(&ps->wsl.queue, wsle, queue_entry);
	RB_REMOVE(vmp_wsle_rb, &ps->wsl.tree, wsle);

	kprintf("Evicting 0x%zx\n", wsle->vaddr);
	ps->wsl.nentries--;

	vmp_fetch_pte(ps, wsle->vaddr, &pte);
	wsl_evict(ps, pte);

	return wsle;
}

/*! true if it could expand, false otherwise */
static bool
wsl_try_expand(eprocess_t *ps) LOCK_REQUIRES(ps->ws_lock)
    LOCK_REQUIRES(vmp_pfn_lock)
{
	if (vmstat.nfree + vmstat.nstandby > vmparam.min_avail_for_expansion) {
		ps->wsl.max += vmparam.ws_page_expansion_count;
		return true;
	}
	else
		return false;
}

void
vmp_wsl_insert(eprocess_t *ps, vaddr_t vaddr, bool locked)
{
	struct vmp_wsle *wsle = NULL;

	kassert(vmp_wsl_find(ps, vaddr) == NULL);
	kassert(ps->wsl.nentries <= ps->wsl.max);

	if (ps->wsl.nentries == ps->wsl.max && wsl_try_expand(ps) == false) {
		wsle = wsl_trim_1(ps);
		kassert(wsle != NULL);
	}

	if (wsle == NULL)
		wsle = kmem_alloc(sizeof(*wsle));

	ps->wsl.nentries++;
	if (locked)
		ps->wsl.nlocked++;

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

int
vmp_wsl_trim_n(eprocess_t *ps, size_t count) LOCK_REQUIRES(ps->ws_lock)
    LOCK_EXCLUDES(vmp_pfn_lock)
{
	for (int i = 0; i < count; i++) {
		struct vmp_wsle *wsle;
		ipl_t ipl = vmp_acquire_pfn_lock();
		wsle = wsl_trim_1(ps);
		vmp_release_pfn_lock(ipl);
		if (wsle == NULL)
			return i;
		kmem_free(wsle, sizeof(struct vmp_wsle));
	}
	return count;
}

void
vmp_wsl_dump(eprocess_t *ps)
{
	struct vmp_wsle *wsle;
	kprintf("WSL: %zu entries\n%zu locked enties:\n", ps->wsl.nentries, ps->wsl.nlocked);
	kprintf("All entries:\n");
	RB_FOREACH (wsle, vmp_wsle_rb, &ps->wsl.tree) {
		kprintf("- 0x%zx\n", wsle->vaddr);
	}
	kprintf("Dynamic Entries:\n");
	TAILQ_FOREACH (wsle, &ps->wsl.queue, queue_entry) {
		kprintf("- 0x%zx\n", wsle->vaddr);
	}
}
