#ifndef KRX_KDK_VM_H
#define KRX_KDK_VM_H

#include <kdk/defs.h>
#include <kdk/queue.h>
#include <kdk/soft.h>

#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

#if KRX_PLATFORM_BITS == 32
#define PFN_BITS 20
#else
#define PFN_BITS 52
#endif

enum vm_page_use {
	/*! Invalid sentinel value. */
	kPageUseInvalid,
	/*! Page is used by the PFN database or otherwise reserved. */
	kPageUsePFNDB,
	/*! Page is free. */
	kPageUseFree,
	/*! Page no longer needed & will be freed on reaching 0 refcount. */
	kPageUseDeleted,
	/*! Page is used by kernel wired memory. */
	kPageUseKWired,
	/*! Page belongs to process-private anonymous memory. */
	kPageUseAnonPrivate,
	/*! Page belongs to a shared anonymous section. */
	kPageUseAnonShared,
	/*! Page belongs to a file cache section. */
	kPageUseFileShared,
	/*! Page belongs to a fork page block. */
	kPageUseForkPage,
	/*! Page is a pagetable (leaf). */
	kPageUsePML1,
	/*! Page is a pagetable (3rd-closest to root). */
	kPageUsePML2,
	/* Page is a pagetable (2nd-closest to root). */
	kPageUsePML3,
	/*! Page is a pagetable (closest to root). */
	kPageUsePML4,
};

/*!
 * PFN database element. Mainly for private use by the VMM, but published here
 * publicly for efficiency.
 */
typedef struct vm_page {
	/* first word */
	struct __attribute__((packed)) {
		pfn_t pfn : PFN_BITS;
		enum vm_page_use use : 4;
		bool dirty : 1;
		bool busy : 1;
		uintptr_t order : 5;
		bool on_freelist : 1;
	};

	/* second word */
	union __attribute__((packed)) {
		/* kPageUsePML* */
		struct __attribute__((packed)) {
			/* Non-zero PTEs */
			uint16_t nonzero_ptes;
			/*! Non-swap PTEs - these keep the page in-core */
			uint16_t nonswap_ptes;
		};
		/* kPageUse*Shared: offset into section */
		uint64_t offset : 48;
	};
	uint16_t refcnt;

	/* third word */
	paddr_t referent_pte;

	/* 4th, 5th words */
	union __attribute__((packed)) {
		/*! Standby/modified/free queue link. */
		TAILQ_ENTRY(vm_page) queue_link;
		/*! If busy, the pager request. */
		struct vmp_pager_state *pager_state;
	};

	/* 6th word */
	union __attribute__((packed)) {
		/*! kPageUsePML* or kPageUseAnonPrivate */
		struct eprocess *process;
		/*! kPageUseAnonShared or kPageUseFileShared */
		struct vm_section *section;
		/*! kPageUseForkPage*/
		struct vmp_forkpage *forkpage;
	};

	/* 7th word */
	uintptr_t swap_descriptor;
} vm_page_t;

/*!
 * Memory descriptor list.
 */
typedef struct vm_mdl {
	size_t nentries;
	size_t offset;
	vm_page_t *pages[0];
} vm_mdl_t;

enum vmp_pte_kind {
	kPTEKindZero,
	kPTEKindTrans,
	kPTEKindSwap,
	kPTEKindBusy,
	kPTEKindValid,
};

int vm_fault(vaddr_t vaddr, bool write, vm_mdl_t *out);

#endif /* KRX_KDK_VM_H */
