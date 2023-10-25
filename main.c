#include <kdk/nanokern.h>
#include <kdk/vm.h>

#include "executive.h"
#include "vm/vmp.h"

__thread ipl_t SIM_ipl = kIPL0;

int
main(int argc, char *arv[])
{
	void SIM_pages_init(void);
	int vmp_wire_pte(eprocess_t *, vaddr_t, struct vmp_pte_wire_state *);
	void vmp_pte_wire_state_release(struct vmp_pte_wire_state *);
	void vm_dump_pages(void);
	void vmp_wsl_dump(eprocess_t * ps);

	SIM_pages_init();

	vm_page_t *page;
	eprocess_t proc;
	vmp_page_alloc_locked(&page, kPageUsePML4, true);
	proc.pml4 = (void *)P2V(vmp_page_paddr(page));
	proc.pml4_page = page;
	RB_INIT(&proc.wsl.tree);
	TAILQ_INIT(&proc.wsl.queue);
	proc.wsl.nlocked = 0;
	pthread_mutex_init(&proc.ws_lock, NULL);

	printf("Wiring round 1\n");
	struct vmp_pte_wire_state state;
	vmp_wire_pte(&proc, 0x0, &state);
	// vmp_pte_wire_state_release(&state);
	vm_dump_pages();

	printf("Wiring round 2\n");
	vmp_wire_pte(&proc, 0x0, &state);
	vmp_pte_wire_state_release(&state);
	vm_dump_pages();

	vmp_wsl_dump(&proc);
}
