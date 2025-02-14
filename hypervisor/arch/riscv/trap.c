/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm/cpu.h>
#include <asm/smp.h>
#include "uart.h"
#include "trap.h"

void sexpt_handler(void)
{
	early_printk("resv sexpt_handler\n");
}

void sswi_handler(void)
{
	int cpu = smp_processor_id();
	char *s = "sswi_handler: d\n";

	s[14] = cpu + '0';
	early_printk(s);
//	asm volatile ("csrwi sip, 0\n\t"::);
}

void reset_stimer(void)
{
	asm volatile(
		"li a0, 1 \n\t" 	\
		"ecall \n\t"		\
		:::
	);
	return;
}

void stimer_handler(void)
{
//	printk("stimer_handler\n");
	reset_stimer();
	hv_timer_handler();
}

void sexti_handler(void)
{
	struct cpu_regs regs;
	dispatch_interrupt(&regs);
	early_printk("sexti_handler\n");
}

static irq_handler_t sirq_handler[] = {
	sexpt_handler,
	sswi_handler,
	sexpt_handler,
	sexpt_handler,
	sexpt_handler,
	stimer_handler,
	sexpt_handler,
	sexpt_handler,
	sexpt_handler,
	sexti_handler,
	sexpt_handler
};

void sint_handler(int irq)
{
	//printk("sint handler\n");
	if (irq < 10)
		sirq_handler[irq]();
	else
		sirq_handler[10]();
}

void vsswi_handler(void)
{
	int cpu;
	char *s = "sswi_handler: d\n";

	asm volatile ("csrr %0, sscratch":"=r"(cpu)::"memory");
	s[14] = cpu + '0';
	early_printk(s);
//	asm volatile ("csrwi sip, 0\n\t"::);
}

static irq_handler_t vsirq_handler[] = {
	sexpt_handler,
	vsswi_handler,
	sexpt_handler,
	sexpt_handler,
	sexpt_handler,
	stimer_handler,
	sexpt_handler,
	sexpt_handler,
	sexpt_handler,
	sexti_handler,
	sexpt_handler
};

void vsint_handler(int irq)
{
	//printk("sint handler\n");
	if (irq < 10)
		vsirq_handler[irq]();
	else
		vsirq_handler[10]();
}
