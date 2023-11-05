/*!
 * @file swapper.c
 * @brief The swapper trims working sets and writes modified anonymous pages.
 */

#include <kdk/nanokern.h>

#include "vmp.h"

#define NS_PER_S 1000000000

kevent_t vmp_swapper_event;

void *
vmp_swapper(void *)
{
loop:
	ke_event_wait(&vmp_swapper_event, NS_PER_S);

	printf("Hello from the swapper\n");

	goto loop;
}
