
#include <kdk/executive.h>
#include <kdk/nanokern.h>

#include "vmp.h"

kevent_t vmp_balancer_event;

void *
vmp_balancer(void *)
{
	int32_t target;
	kwaitstatus_t w;
	ipl_t ipl;

loop:
	w = ke_event_wait(&vmp_balancer_event, NS_PER_S);

	printf("Balancer wakes\n");

	if (w == kKernWaitStatusOK)
		target = vmparam.ws_page_expansion_count * 1;
	else
		goto loop;

	for (int i = 0; i < 1; i++) {
		ke_wait(&kernel_ps.ws_lock, "vmp_balancer:ps->ws_lock", false,
		    false, -1);
		target -= vmp_wsl_trim_n(&kernel_ps, target);
		ke_mutex_release(&kernel_ps.ws_lock);
	}

	ipl = vmp_acquire_pfn_lock();
	if (vmp_page_sufficience())
		ke_event_clear(&vmp_balancer_event);
	vmp_release_pfn_lock(ipl);

	goto loop;
}
