#ifndef KRX_VM_SOFT_H
#define KRX_VM_SOFT_H

#include <kdk/nanokern.h>
#include <kdk/vm.h>

#define VMP_TABLE_LEVELS 4
#define VMP_PAGE_SHIFT 12

typedef struct pte_hw {
	bool valid : 1;
	bool writeable : 1;
	bool user : 1;
	bool writethrough : 1;
	bool nocache : 1;
	bool accessed : 1;
	bool dirty : 1;
	bool pat : 1;
	bool global : 1;
	uint64_t available1 : 3;
	pfn_t pfn : 40;
	uint64_t available2 : 11;
	bool nx : 1;
} pte_hw_t;

enum vmp_soft_pte_kind {
	kSoftPteKindSwap,
	kSoftPteKindBusy,
	kSoftPteKindTrans,
};

typedef struct pte_swap {
	bool valid : 1;
	uintptr_t swap_desc : 61;
	enum vmp_soft_pte_kind kind : 2;
} pte_swap_t;

typedef struct pte_busy {
	bool valid : 1;
	vaddr_t state : 61;
	enum vmp_soft_pte_kind kind : 2;
} pte_busy_t;

typedef struct pte_trans {
	bool valid : 1;
	pfn_t pfn : 61;
	enum vmp_soft_pte_kind kind : 2;
} pte_trans_t;

typedef union pte {
	pte_hw_t hw;
	pte_swap_t swap;
	pte_busy_t busy;
	pte_trans_t trans;
	uint64_t u64;
} pte_t;

union vmp_vaddr {
	struct {
		uintptr_t pgi : 12;
		uintptr_t pml1i : 9;
		uintptr_t pml2i : 9;
		uintptr_t pml3i : 9;
		uintptr_t pml4i : 9;
		uintptr_t reserved : 16;
	};
	uintptr_t addr;
};

static inline enum vmp_pte_kind
vmp_pte_characterise(pte_t *pte)
{
	if (pte->u64 == 0x0)
		return kPTEKindZero;
	else if (pte->hw.valid)
		return kPTEKindValid;
	else if (pte->busy.kind == kSoftPteKindBusy)
		return kPTEKindBusy;
	else if (pte->trans.kind == kSoftPteKindTrans)
		return kPTEKindTrans;
	else {
		kassert(pte->swap.kind == kSoftPteKindSwap);
		return kPTEKindSwap;
	}
}

static inline pfn_t
vmp_pte_hw_pfn(pte_t *pte, int level)
{
	return pte->hw.pfn;
}

static inline void
vmp_pte_hw_create(pte_t *pte, pfn_t pfn, bool writeable)
{
	pte->u64 = 0x0;
	pte->hw.valid = 1;
	pte->hw.writeable = 1;
	pte->hw.pfn = pfn;
}

static inline void
vmp_pte_zero_create(pte_t *pte)
{
	pte->u64 = 0x0;
}

static inline void
vmp_addr_unpack(vaddr_t vaddr, int unpacked[5])
{
	union vmp_vaddr addr;
	addr.addr = vaddr;
	unpacked[0] = addr.pgi;
	unpacked[1] = addr.pml1i;
	unpacked[2] = addr.pml2i;
	unpacked[3] = addr.pml3i;
	unpacked[4] = addr.pml4i;
}

/* vmp_pager_state_t *vmp_pte_busy_state(pte_t *pte) */
#define vmp_pte_busy_state(PTE) (vmp_pager_state_t *)(pte->trans.state << 4)

#endif /* KRX_VM_SOFT_H */
