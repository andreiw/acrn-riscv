/*-
 * Copyright (c) 2011 NetApp, Inc.
 * Copyright (c) 2017 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <types.h>
#include <x86/lib/atomic.h>
#include <x86/pgtable.h>
#include <x86/cpu_caps.h>
#include <x86/mmu.h>
#include <x86/vmx.h>
#include <reloc.h>
#include <x86/guest/vm.h>
#include <x86/boot/ld_sym.h>
#include <logmsg.h>
#include <misc_cfg.h>

static void *ppt_mmu_pml4_addr;
static uint8_t sanitized_page[PAGE_SIZE] __aligned(PAGE_SIZE);

/* PPT VA and PA are identical mapping */
#define PPT_PML4_PAGE_NUM	PML4_PAGE_NUM(MAX_PHY_ADDRESS_SPACE)
#define PPT_PDPT_PAGE_NUM	PDPT_PAGE_NUM(MAX_PHY_ADDRESS_SPACE)
/* Please refer to how the EPT_PD_PAGE_NUM was calculated */
#define PPT_PD_PAGE_NUM	(PD_PAGE_NUM(CONFIG_PLATFORM_RAM_SIZE + (MEM_4G)) + \
			CONFIG_MAX_PCI_DEV_NUM * 6U)
#define PPT_PT_PAGE_NUM	0UL	/* not support 4K granularity page mapping */
/* must be a multiple of 64 */
#define PPT_PAGE_NUM	(roundup((PPT_PML4_PAGE_NUM + PPT_PDPT_PAGE_NUM + \
			PPT_PD_PAGE_NUM + PPT_PT_PAGE_NUM), 64U))
static struct page ppt_pages[PPT_PAGE_NUM];
static uint64_t ppt_page_bitmap[PPT_PAGE_NUM / 64];

/* ppt: pripary page pool */
static struct page_pool ppt_page_pool = {
	.start_page = ppt_pages,
	.bitmap_size = PPT_PAGE_NUM / 64,
	.bitmap = ppt_page_bitmap,
	.last_hint_id = 0UL,
	.dummy_page = NULL,
};

/* @pre: The PPT and EPT have same page granularity */
static inline bool ppt_large_page_support(enum _page_table_level level, __unused uint64_t prot)
{
	bool support;

	if (level == IA32E_PD) {
		support = true;
	} else if (level == IA32E_PDPT) {
		support = pcpu_has_vmx_ept_cap(VMX_EPT_1GB_PAGE);
	} else {
		support = false;
	}

	return support;
}

static inline void ppt_clflush_pagewalk(const void* entry __attribute__((unused)))
{
}

static inline uint64_t ppt_pgentry_present(uint64_t pte)
{
	return pte & PAGE_PRESENT;
}

static inline void ppt_nop_tweak_exe_right(uint64_t *entry __attribute__((unused))) {}
static inline void ppt_nop_recover_exe_right(uint64_t *entry __attribute__((unused))) {}

static const struct pgtable ppt_pgtable = {
	.default_access_right = (PAGE_PRESENT | PAGE_RW | PAGE_USER),
	.pool = &ppt_page_pool,
	.large_page_support = ppt_large_page_support,
	.pgentry_present = ppt_pgentry_present,
	.clflush_pagewalk = ppt_clflush_pagewalk,
	.tweak_exe_right = ppt_nop_tweak_exe_right,
	.recover_exe_right = ppt_nop_recover_exe_right,
};

#define INVEPT_TYPE_SINGLE_CONTEXT      1UL
#define INVEPT_TYPE_ALL_CONTEXTS        2UL
#define VMFAIL_INVALID_EPT_VPID				\
	"       jnc 1f\n"				\
	"       mov $1, %0\n"    /* CF: error = 1 */	\
	"       jmp 3f\n"				\
	"1:     jnz 2f\n"				\
	"       mov $2, %0\n"    /* ZF: error = 2 */	\
	"       jmp 3f\n"				\
	"2:     mov $0, %0\n"				\
	"3:"

struct invvpid_operand {
	uint32_t vpid : 16;
	uint32_t rsvd1 : 16;
	uint32_t rsvd2 : 32;
	uint64_t gva;
};

struct invept_desc {
	uint64_t eptp;
	uint64_t res;
};

static inline int32_t asm_invvpid(const struct invvpid_operand operand, uint64_t type)
{
	int32_t error;
	asm volatile ("invvpid %1, %2\n"
			VMFAIL_INVALID_EPT_VPID
			: "=r" (error)
			: "m" (operand), "r" (type)
			: "memory");
	return error;
}

/*
 * @pre: the combined type and vpid is correct
 */
static inline void local_invvpid(uint64_t type, uint16_t vpid, uint64_t gva)
{
	const struct invvpid_operand operand = { vpid, 0U, 0U, gva };

	if (asm_invvpid(operand, type) != 0) {
		pr_dbg("%s, failed. type = %lu, vpid = %u", __func__, type, vpid);
	}
}

static inline int32_t asm_invept(uint64_t type, struct invept_desc desc)
{
	int32_t error;
	asm volatile ("invept %1, %2\n"
			VMFAIL_INVALID_EPT_VPID
			: "=r" (error)
			: "m" (desc), "r" (type)
			: "memory");
	return error;
}

/*
 * @pre: the combined type and EPTP is correct
 */
static inline void local_invept(uint64_t type, struct invept_desc desc)
{
	if (asm_invept(type, desc) != 0) {
		pr_dbg("%s, failed. type = %lu, eptp = 0x%lx", __func__, type, desc.eptp);
	}
}

void flush_vpid_single(uint16_t vpid)
{
	if (vpid != 0U) {
		local_invvpid(VMX_VPID_TYPE_SINGLE_CONTEXT, vpid, 0UL);
	}
}

void flush_vpid_global(void)
{
	local_invvpid(VMX_VPID_TYPE_ALL_CONTEXT, 0U, 0UL);
}

void invept(const void *eptp)
{
	struct invept_desc desc = {0};

	if (pcpu_has_vmx_ept_cap(VMX_EPT_INVEPT_SINGLE_CONTEXT)) {
		desc.eptp = hva2hpa(eptp) | (3UL << 3U) | 6UL;
		local_invept(INVEPT_TYPE_SINGLE_CONTEXT, desc);
	} else if (pcpu_has_vmx_ept_cap(VMX_EPT_INVEPT_GLOBAL_CONTEXT)) {
		local_invept(INVEPT_TYPE_ALL_CONTEXTS, desc);
	} else {
		/* Neither type of INVEPT is supported. Skip. */
	}
}

void enable_paging(void)
{
	uint64_t tmp64 = 0UL;

	/*
	 * Enable MSR IA32_EFER.NXE bit,to prevent
	 * instruction fetching from pages with XD bit set.
	 */
	tmp64 = msr_read(MSR_IA32_EFER);
	tmp64 |= MSR_IA32_EFER_NXE_BIT;
	msr_write(MSR_IA32_EFER, tmp64);

	/* Enable Write Protect, inhibiting writing to read-only pages */
	CPU_CR_READ(cr0, &tmp64);
	CPU_CR_WRITE(cr0, tmp64 | CR0_WP);

	/* HPA->HVA is 1:1 mapping at this moment, simply treat ppt_mmu_pml4_addr as HPA. */
	CPU_CR_WRITE(cr3, ppt_mmu_pml4_addr);
}

void enable_smep(void)
{
	uint64_t val64 = 0UL;

	/* Enable CR4.SMEP*/
	CPU_CR_READ(cr4, &val64);
	CPU_CR_WRITE(cr4, val64 | CR4_SMEP);
}

void enable_smap(void)
{
	uint64_t val64 = 0UL;

	/* Enable CR4.SMAP*/
	CPU_CR_READ(cr4, &val64);
	CPU_CR_WRITE(cr4, val64 | CR4_SMAP);
}

/*
 * Clean USER bit in page table to update memory pages to be owned by hypervisor.
 */
void ppt_clear_user_bit(uint64_t base, uint64_t size)
{
	uint64_t base_aligned;
	uint64_t size_aligned;
	uint64_t region_end = base + size;

	/*rounddown base to 2MBytes aligned.*/
	base_aligned = round_pde_down(base);
	size_aligned = region_end - base_aligned;

	pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr, base_aligned,
		round_pde_up(size_aligned), 0UL, PAGE_USER, &ppt_pgtable, MR_MODIFY);
}

void ppt_set_nx_bit(uint64_t base, uint64_t size, bool add)
{
	uint64_t region_end = base + size;
	uint64_t base_aligned = round_pde_down(base);
	uint64_t size_aligned = round_pde_up(region_end - base_aligned);

	if (add) {
		pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr,
			base_aligned, size_aligned, PAGE_NX, 0UL, &ppt_pgtable, MR_MODIFY);
	} else {
		pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr,
			base_aligned, size_aligned, 0UL, PAGE_NX, &ppt_pgtable, MR_MODIFY);
	}
}

void init_paging(void)
{
	uint64_t hv_hva;
	uint32_t i;
	uint64_t low32_max_ram = 0UL;
	uint64_t high64_max_ram = MEM_4G;

	const struct e820_entry *entry;
	uint32_t entries_count = get_e820_entries_count();
	const struct e820_entry *p_e820 = get_e820_entry();

	pr_dbg("HV MMU Initialization");

	init_sanitized_page((uint64_t *)sanitized_page, hva2hpa_early(sanitized_page));

	/* Allocate memory for Hypervisor PML4 table */
	ppt_mmu_pml4_addr = pgtable_create_root(&ppt_pgtable);

	/* Modify WB attribute for E820_TYPE_RAM */
	for (i = 0U; i < entries_count; i++) {
		entry = p_e820 + i;
		if (entry->type == E820_TYPE_RAM) {
			uint64_t end = entry->baseaddr + entry->length;
			if (end < MEM_4G) {
				low32_max_ram = max(end, low32_max_ram);
			} else {
				high64_max_ram = max(end, high64_max_ram);
			}
		}
	}

	low32_max_ram = round_pde_up(low32_max_ram);
	high64_max_ram = round_pde_down(high64_max_ram);

	/* Map [0, low32_max_ram) and [4G, high64_max_ram) RAM regions as WB attribute */
	pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, 0UL, 0UL,
			low32_max_ram, PAGE_ATTR_USER | PAGE_CACHE_WB, &ppt_pgtable);
	pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, MEM_4G, MEM_4G,
			high64_max_ram - MEM_4G, PAGE_ATTR_USER | PAGE_CACHE_WB, &ppt_pgtable);

	/* Map [low32_max_ram, 4G) and [HI_MMIO_START, HI_MMIO_END) MMIO regions as UC attribute */
	pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, low32_max_ram, low32_max_ram,
		MEM_4G - low32_max_ram, PAGE_ATTR_USER | PAGE_CACHE_UC, &ppt_pgtable);
	if ((HI_MMIO_START != ~0UL) && (HI_MMIO_END != 0UL)) {
		pgtable_add_map((uint64_t *)ppt_mmu_pml4_addr, HI_MMIO_START, HI_MMIO_START,
			(HI_MMIO_END - HI_MMIO_START), PAGE_ATTR_USER | PAGE_CACHE_UC, &ppt_pgtable);
	}

	/*
	 * set the paging-structure entries' U/S flag to supervisor-mode for hypervisor owned memroy.
	 * (exclude the memory reserve for trusty)
	 *
	 * Before the new PML4 take effect in enable_paging(), HPA->HVA is always 1:1 mapping,
	 * simply treat the return value of get_hv_image_base() as HPA.
	 */
	hv_hva = get_hv_image_base();
	pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr, hv_hva & PDE_MASK,
			CONFIG_HV_RAM_SIZE + (((hv_hva & (PDE_SIZE - 1UL)) != 0UL) ? PDE_SIZE : 0UL),
			PAGE_CACHE_WB, PAGE_CACHE_MASK | PAGE_USER, &ppt_pgtable, MR_MODIFY);

	/*
	 * remove 'NX' bit for pages that contain hv code section, as by default XD bit is set for
	 * all pages, including pages for guests.
	 */
	pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr, round_pde_down(hv_hva),
			round_pde_up((uint64_t)&ld_text_end) - round_pde_down(hv_hva), 0UL,
			PAGE_NX, &ppt_pgtable, MR_MODIFY);
#if (SOS_VM_NUM == 1)
	pgtable_modify_or_del_map((uint64_t *)ppt_mmu_pml4_addr, (uint64_t)get_sworld_memory_base(),
			TRUSTY_RAM_SIZE * MAX_POST_VM_NUM, PAGE_USER, 0UL, &ppt_pgtable, MR_MODIFY);
#endif

	/* Enable paging */
	enable_paging();
}

/*
 * @pre: addr != NULL  && size != 0
 */
void flush_address_space(void *addr, uint64_t size)
{
	uint64_t n = 0UL;

	while (n < size) {
		clflushopt((char *)addr + n);
		n += CACHE_LINE_SIZE;
	}
}
