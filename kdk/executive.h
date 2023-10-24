#ifndef KRX_KDK_EXECUTIVE_H
#define KRX_KDK_EXECUTIVE_H

#include <kdk/defs.h>
#include <kdk/nanokern.h>

typedef struct eprocess {
	kmutex_t ws_lock;
	void *pml4;
	struct vm_page *pml4_page;
} eprocess_t;

#endif /* KRX_KDK_EXECUTIVE_H */
