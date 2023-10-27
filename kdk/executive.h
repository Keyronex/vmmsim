#ifndef KRX_KDK_EXECUTIVE_H
#define KRX_KDK_EXECUTIVE_H

#include <kdk/defs.h>
#include <kdk/nanokern.h>
#include <kdk/queue.h>
#include <kdk/tree.h>

RB_HEAD(vm_vad_rbtree, vm_vad);

typedef struct eprocess {
	kmutex_t vad_lock;
	struct vm_vad_rbtree vad_tree;
	kmutex_t ws_lock;
	void *pml4;
	struct vm_page *pml4_page;
	struct {
		TAILQ_HEAD(, vmp_wsle) queue;
		RB_HEAD(vmp_wsle_rb, vmp_wsle) tree;
		size_t nlocked;
	} wsl;
} eprocess_t;

#endif /* KRX_KDK_EXECUTIVE_H */
