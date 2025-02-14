/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <irq.h>
#include <errno.h>
#include <asm/init.h>
#include <asm/irq.h>
#include <asm/lib/bits.h>
#include <debug/logmsg.h>

const unsigned int nr_irqs = NR_IRQS;

static spinlock_t riscv_irq_spinlock = { .head = 0U, .tail = 0U, };
static struct arch_irq_desc irq_data[NR_IRQS];
static struct irq_desc irq_desc[NR_IRQS];

struct acrn_irqchip_ops dummy_irqchip;

struct irq_desc *__irq_to_desc(int irq)
{
	return &irq_desc[irq];
}

static int arch_data_init_one_irq_desc(struct irq_desc *desc)
{
	((struct arch_irq_desc *)desc->arch_data)->type = IRQ_TYPE_INVALID;
	return 0;
}

uint32_t irq_to_vector(uint32_t irq)
{
	uint64_t rflags;
	uint32_t ret = VECTOR_INVALID;

	if (irq < NR_IRQS) {
		spinlock_irqsave_obtain(&riscv_irq_spinlock, &rflags);
		ret = irq_data[irq].vector;
		spinlock_irqrestore_release(&riscv_irq_spinlock, rflags);
	}

	return ret;
}

/*
 * @ Could be common API
 * */
int init_one_irq_desc(struct irq_desc *desc)
{
	int err;

	if (irq_desc_initialized(desc)) {
		return 0;
	}

	set_bit(_IRQ_DISABLED, &((struct arch_irq_desc *)desc->arch_data)->status);
	((struct arch_irq_desc *)desc->arch_data)->irqchip = &dummy_irqchip;

	err = arch_data_init_one_irq_desc(desc);
	if (err) {
		((struct arch_irq_desc *)desc->arch_data)->irqchip = NULL;
	}

	return err;
}

/* only run on bsp boot*/
static void __init init_irq_data(void)
{
	int irq;

	for (irq = 0; irq < NR_IRQS; irq++) {
		struct irq_desc *desc = irq_to_desc(irq);
		init_one_irq_desc(desc);
		desc->irq = irq;
		desc->action  = NULL;
	}
}

void __init init_IRQ(void)
{
	init_irq_data();
}

int irq_set_type(unsigned int irq, unsigned int type)
{
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	((struct arch_irq_desc *)desc->arch_data)->type = type;
	return 0;
}

bool request_irq_arch(uint32_t irq)
{
	return true;
}

void pre_irq_arch(const struct irq_desc *desc)
{
}

void post_irq_arch(const struct irq_desc *desc)
{
}

void do_IRQ(struct cpu_regs *regs, unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	spin_lock(&desc->lock);

	if (test_bit(_IRQ_DISABLED, ((struct arch_irq_desc *)desc->arch_data)->status)) {
		pr_dbg("irq is disabled");
		goto out;
	}

	set_bit(_IRQ_INPROGRESS, &((struct arch_irq_desc *)desc->arch_data)->status);

	// run handler with irq enabled.
	spin_unlock_irq(&desc->lock);
	if (desc->action)
		desc->action(irq, regs);

	// disable irq
	spin_lock_irq(&desc->lock);
	clear_bit(_IRQ_INPROGRESS, &((struct arch_irq_desc *)desc->arch_data)->status);

out:
	((struct arch_irq_desc *)desc->arch_data)->irqchip->eoi(desc);
	spin_unlock(&desc->lock);
}

struct acrn_irqchip_ops *acrn_irqchip;

/* must be called after IRQ setup */
void setup_irqs_arch(void)
{
}

void dispatch_interrupt(struct cpu_regs *regs)
{
	uint32_t irq;
	do {
		irq = acrn_irqchip->get_irq();
		if (irq == 0)
			break;
		pr_dbg("dispatch interrupt: %d",irq);
		do_IRQ(regs, irq);
	} while (1);
}

/*
 * descs[] must have NR_IRQS entries
 */
void init_irq_descs_arch(struct irq_desc descs[])
{
	uint32_t i;

	for (i = 0U; i < NR_IRQS; i++) {
		irq_data[i].vector = VECTOR_INVALID;
		descs[i].arch_data = (void *)&irq_data[i];
	}
}

void init_interrupt_arch(uint16_t pcpu_id)
{
}

void free_irq_arch(uint32_t irq)
{
}
