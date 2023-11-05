#include <kdk/nanokern.h>
#include <kdk/vm.h>

#include "executive.h"
#include "vm/vmp.h"

__thread ipl_t SIM_ipl = kIPL0;
pthread_t swapper_thread;
eprocess_t kernel_ps;

int
main(int argc, char *arv[])
{
	void SIM_pages_init(void);
	void vm_dump_pages(void);
	void vmp_wsl_dump(eprocess_t * ps);
	void *vmp_swapper(void*);

	SIM_pages_init();

	vm_page_t *page;
	vmp_page_alloc_locked(&page, kPageUsePML4, true);
	kernel_ps.pml4 = (void *)P2V(vmp_page_paddr(page));
	kernel_ps.pml4_page = page;
	RB_INIT(&kernel_ps.wsl.tree);
	TAILQ_INIT(&kernel_ps.wsl.queue);
	kernel_ps.wsl.nlocked = 0;
	kernel_ps.wsl.nentries = 0;
	kernel_ps.wsl.max = 4;
	pthread_mutex_init(&kernel_ps.ws_lock, NULL);

	ke_event_init(&vmp_swapper_event, false);
	pthread_create(&swapper_thread, NULL, vmp_swapper, NULL);

#if 0
	printf("Wiring round 1\n");
	struct vmp_pte_wire_state state;
	vmp_wire_pte(&kernel_ps, 0x0, &state);
	vm_dump_pages();
	printf("Now unwire.\n");
	vmp_pte_wire_state_release(&state);
	vm_dump_pages();

	printf("Wiring round 2\n");
	vmp_wire_pte(&kernel_ps, 0x0, &state);
	vmp_pte_wire_state_release(&state);
	vm_dump_pages();
#endif

	vaddr_t vaddr = 0x0;
	vm_ps_allocate(&kernel_ps, &vaddr, PGSIZE * 32, true);

	vm_fault(0x0, false, NULL);
	vm_fault(PGSIZE, false, NULL);
	vm_fault(PGSIZE * 2, false, NULL);
	vm_fault(PGSIZE, false, NULL);

	vmp_wsl_dump(&kernel_ps);
	vm_dump_pages();

	for (;;) ;
}
