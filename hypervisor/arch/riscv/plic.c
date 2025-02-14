/*
 * Copyright (C) 2023-2024 Intel Corporation. All rights reserved.
 *
 * SVPN1X-License-Identifier: BSD-3-Clause
 */

#include <asm/irq.h>
#include <debug/logmsg.h>
#include <asm/per_cpu.h>
#include <asm/guest/vcpu.h>
// mmu related
#include <asm/init.h>
#include <asm/mem.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/plic.h>
#include <asm/lib/spinlock.h>
#include <asm/lib/bits.h>
#include <asm/io.h>

extern DEFINE_PAGE_TABLE(acrn_pgtable);
// mmu related end

#define NR_PLIC_CPU 5
struct acrn_plic phy_plic;
struct acrn_plic *plic = &phy_plic;

void plic_write8(struct acrn_plic *plic, uint32_t value, uint32_t offset)
{
	writeb_relaxed(value, plic->map_base + offset);
}

void plic_write32(struct acrn_plic *plic, uint32_t value, uint32_t offset)
{
	writel_relaxed(value, plic->map_base + offset);
}

uint32_t plic_read32(struct acrn_plic *plic, uint32_t offset)
{
	return readl_relaxed(plic->map_base + offset);
}

static void plic_set_irq(struct irq_desc *irqd, uint32_t offset)
{
	uint32_t base, val;

	base = offset + (irqd->irq / 32) * 4;
	val = plic_read32(plic, base);
	val |= 1U << (irqd->irq % 32);
	// 1bits/IRQ
	plic_write32(plic, val, base);
}

static void plic_clear_irq(struct irq_desc *irqd, uint32_t offset)
{
	uint32_t base, val;

	base = offset + (irqd->irq / 32) * 4;
	val = plic_read32(plic, base);
	val &= ~(1U << (irqd->irq % 32));
	// 1bits/IRQ
	plic_write32(plic, val, base);
}

void plic_set_address(void)
{
	plic->base = CONFIG_PLIC_BASE; 
	plic->size = CONFIG_PLIC_SIZE;
}

void plic_init_map(void){
	plic->map_base = hpa2hva(plic->base);
}

static void plic_set_irq_mask(struct irq_desc *desc, uint32_t priority)
{
	spin_lock(&plic->lock);
	plic_write32(plic, priority & 0x7, PLIC_THR);
	spin_unlock(&plic->lock);
}

static void plic_set_irq_priority(struct irq_desc *desc, uint32_t priority)
{
	unsigned int irq = desc->irq;

	spin_lock(&plic->lock);
	plic_write32(plic, priority & 0x7, PLIC_IPRR + irq * 4);
	spin_unlock(&plic->lock);
}

static void plic_irq_enable(struct irq_desc *desc)
{
	unsigned long flags;

	spin_lock_irqsave(&plic->lock, &flags);
	plic_set_irq(desc, PLIC_IER);
	clear_bit(_IRQ_DISABLED, &((struct arch_irq_desc *)desc->arch_data)->status);
	dsb();
	spin_unlock_irqrestore(&plic->lock, flags);
}

static void plic_irq_disable(struct irq_desc *desc)
{
	unsigned long flags;

	spin_lock_irqsave(&plic->lock, &flags);
	plic_clear_irq(desc, PLIC_IER);
	set_bit(_IRQ_DISABLED, &((struct arch_irq_desc *)desc->arch_data)->status);
	spin_unlock_irqrestore(&plic->lock, flags);
}

static uint32_t plic_get_irq(void)
{
	return plic_read32(plic, PLIC_EOIR);
}

static void plic_eoi_irq(struct irq_desc *desc)
{
	plic_write32(plic, desc->irq, PLIC_EOIR);
}

struct acrn_irqchip_ops plic_ops = {
	.name     		= "sifive-plic",
	.init			= plic_init,
	.set_irq_mask 		= plic_set_irq_mask,
	.set_irq_priority 	= plic_set_irq_priority,
	.get_irq 		= plic_get_irq,
	.enable       		= plic_irq_enable,
	.disable      		= plic_irq_disable,
	.eoi			= plic_eoi_irq,
};

void plic_init(void)
{
	acrn_irqchip = &plic_ops;
	plic_set_address();
	pr_info("plic"
		"base: %lx"
		"size: %lx",
		plic->base,
		plic->size);
	spinlock_init(&plic->lock);
	spin_lock(&plic->lock);
	plic_init_map();
	spin_unlock(&plic->lock);
}
