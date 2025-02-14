/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __RISCV_SYSTEM_H__
#define __RISCV_SYSTEM_H__

#ifndef __ASSEMBLY__

#define sev()           asm volatile("nop" : : : "memory")
#define wfe()           asm volatile("nop" : : : "memory")
#define wfi()           asm volatile("wfi" : : : "memory")

#define isb()           asm volatile("fence.i" : : : "memory")
#define dsb()		asm volatile("fence" : : : "memory")
#define dmb()		asm volatile("fence" : : : "memory")

#define mb()            dsb()
#define rmb()           dsb()
#define wmb()           dsb()

#define smp_mb()        dmb()
#define smp_rmb()       dmb()
#define smp_wmb()       dmb()

#define local_irq_disable()   asm volatile ( "csrc sstatus, 0x2\n" :::)
#define local_irq_enable()    asm volatile ( "csrs sstatus, 0x2\n" :::)

#define local_save_flags(x)					\
({								\
	asm volatile ("csrr t0, sstatus\n\t"			\
		      "sd t0, (%0)\n"				\
		      ::"r"((unsigned long)x):"memory", "t0");	\
})

#define local_irq_save(x)					\
({								\
	local_save_flags(x);					\
	local_irq_disable();					\
})

#define local_irq_restore(x)					\
({								\
	asm volatile ( "csrw sstatus, %0\n" ::"r"(x):);		\
})

static inline int local_irq_is_enabled(void)
{
	unsigned long flags;
	local_save_flags(flags);
	return !(flags & 0x2);
}

struct acrn_vcpu;
extern struct acrn_vcpu *__context_switch(struct acrn_vcpu *prev, struct acrn_vcpu *next);

/* Synchronizes all write accesses to memory */
static inline void cpu_write_memory_barrier(void)
{
	asm volatile ("fence\n" : : : "memory");
}

/* Synchronizes all read and write accesses to/from memory */
static inline void cpu_memory_barrier(void)
{
	asm volatile ("fence\n" : : : "memory");
}
#endif /* !__ASSEMBLY__ */

#endif /* __RISCV_SYSTEM_H__ */
