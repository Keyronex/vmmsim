#include <kdk/nanokern.h>
#include <kdk/vm.h>

#include "executive.h"
#include "vm/vmp.h"

__thread ipl_t SIM_ipl = kIPL0;
pthread_t pgwriter_thread, balancer_thread;
eprocess_t kernel_ps;

void
access(paddr_t addr, bool for_write)
{
	pte_t *l4, *l3, *l2, *l1, *pte;
	paddr_t final_addr;
	union vmp_vaddr unpacked;
	ipl_t ipl;

	unpacked.addr = addr;

retry:
	l4 = (pte_t *)kernel_ps.pml4;
	if (!l4[unpacked.pml4i].hw.valid) {
		printf("mmu: invalid entry in pml4\n");
		vm_fault(addr, for_write, NULL);
		goto retry;
	}

	l3 = (pte_t *)P2V(vmp_pte_hw_paddr(&l4[unpacked.pml4i], 4));
	if (!l3[unpacked.pml3i].hw.valid) {
		printf("mmu: invalid entry in pml3\n");
		vm_fault(addr, for_write, NULL);
		goto retry;
	}

	l2 = (pte_t *)P2V(vmp_pte_hw_paddr(&l3[unpacked.pml3i], 3));
	if (!l2[unpacked.pml2i].hw.valid) {
		printf("mmu: invalid entry in pml2\n");
		vm_fault(addr, for_write, NULL);
		goto retry;
	}

	l1 = (pte_t *)P2V(vmp_pte_hw_paddr(&l2[unpacked.pml2i], 2));
	if (!l1[unpacked.pml1i].hw.valid) {
		printf("mmu: invalid entry in pml1\n");
		vm_fault(addr, for_write, NULL);
		goto retry;
	} else if (for_write && !l1[unpacked.pml1i].hw.writeable) {
		printf("mmu: write protected\n");
		vm_fault(addr, for_write, NULL);
		goto retry;
	}

	final_addr = vmp_pte_hw_paddr(&l1[unpacked.pml1i], 1);

	printf("mmu: %s 0x%zx => 0x%zx\n", for_write ? "write" : "read ", addr,
	    final_addr + unpacked.pgi);
}

int
main(int argc, char *arv[])
{
	void SIM_pages_init(void);
	void SIM_paging_init(void);
	void vm_dump_pages(void);
	void vmp_wsl_dump(eprocess_t * ps);
	void *vmp_pgwriter(void *), *vmp_balancer(void *);

	SIM_pages_init();
	SIM_paging_init();

	vm_page_t *page;
	vmp_page_alloc_locked(&page, kPageUsePML4, true);
	kernel_ps.pml4 = (void *)P2V(vmp_page_paddr(page));
	kernel_ps.pml4_page = page;
	vmparam.ws_page_expansion_count = 4;
	vmparam.min_avail_for_expansion = 8;
	vmparam.min_avail_for_alloc = 4;
	RB_INIT(&kernel_ps.wsl.tree);
	TAILQ_INIT(&kernel_ps.wsl.queue);
	kernel_ps.wsl.nlocked = 0;
	kernel_ps.wsl.nentries = 0;
	kernel_ps.wsl.max = 4;
	pthread_mutex_init(&kernel_ps.ws_lock, NULL);

	ke_event_init(&vmp_balancer_event, false);
	ke_event_init(&vmp_pgwriter_event, false);
	ke_event_init(&vmp_sufficient_pages_event, false);
	pthread_create(&pgwriter_thread, NULL, vmp_pgwriter, NULL);
	pthread_create(&balancer_thread, NULL, vmp_balancer, NULL);

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
	vm_ps_allocate(&kernel_ps, &vaddr, 4294967296 * 32, true);

#if 1
	for (int i = 0; i < 10; i++) {
		for (int j = 0; j < 9; j++) {
			bool write = true;
			access(PGSIZE * j, write);
			access(4294967296 + PGSIZE * j, write);
			access(4294967296 * 2 + PGSIZE * j, write);
		}
	}
#endif

	vmp_wsl_dump(&kernel_ps);
	vm_dump_pages();
	vm_dump_page_summary();

	kprintf("Simulation complete.\n");
}
