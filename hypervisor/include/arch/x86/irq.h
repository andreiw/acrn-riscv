/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef ARCH_IRQ_H
#define ARCH_IRQ_H

#include <common/irq.h>

/* vectors for normal, usually for devices */
#define VECTOR_FOR_NOR_LOWPRI_START	0x20U
#define VECTOR_FOR_NOR_LOWPRI_END	0x7FU
#define VECTOR_FOR_NOR_HIGHPRI_START	0x80U
#define VECTOR_FOR_NOR_HIGHPRI_END	0xDFU
#define VECTOR_FOR_NOR_END		VECTOR_FOR_NOR_HIGHPRI_END

#define VECTOR_FOR_INTR_START		VECTOR_FOR_NOR_LOWPRI_START

/* vectors for priority, usually for HV service */
#define VECTOR_FOR_PRI_START	0xE0U
#define VECTOR_FOR_PRI_END	0xFFU
#define VECTOR_TIMER		0xEFU
#define VECTOR_NOTIFY_VCPU	0xF0U
#define VECTOR_VIRT_IRQ_VHM	0xF7U
#define VECTOR_SPURIOUS		0xFFU

#define NR_MAX_VECTOR		0xFFU
#define VECTOR_INVALID		(NR_MAX_VECTOR + 1U)
#define NR_IRQS		(256U + 16U)
#define IRQ_INVALID		0xffffffffU

#define DEFAULT_DEST_MODE	IOAPIC_RTE_DESTLOG
#define DEFAULT_DELIVERY_MODE	IOAPIC_RTE_DELLOPRI
#define ALL_CPUS_MASK		((1U << phys_cpu_num) - 1U)

/*
 * Definition of the stack frame layout
 */
struct intr_excp_ctx {
	struct cpu_gp_regs gp_regs;
	uint64_t vector;
	uint64_t error_code;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t ss;
};

int handle_level_interrupt_common(struct irq_desc *desc,
	__unused void *handler_data);
int common_handler_edge(struct irq_desc *desc, __unused void *handler_data);
int common_dev_handler_level(struct irq_desc *desc,
	__unused void *handler_data);
int quick_handler_nolock(struct irq_desc *desc, __unused void *handler_data);

void init_default_irqs(uint16_t cpu_id);

void dispatch_exception(struct intr_excp_ctx *ctx);
void dispatch_interrupt(struct intr_excp_ctx *ctx);

void setup_notification(void);

typedef void (*spurious_handler_t)(uint32_t vector);
extern spurious_handler_t spurious_handler;

/*
 * Some MSI message definitions
 */
#define	MSI_ADDR_MASK	0xfff00000U
#define	MSI_ADDR_BASE	0xfee00000U
#define	MSI_ADDR_RH	0x00000008U	/* Redirection Hint */
#define	MSI_ADDR_LOG	0x00000004U	/* Destination Mode */

/* RFLAGS */
#define HV_ARCH_VCPU_RFLAGS_IF              (1U<<9)

/* Interruptability State info */
#define HV_ARCH_VCPU_BLOCKED_BY_MOVSS       (1U<<1)
#define HV_ARCH_VCPU_BLOCKED_BY_STI         (1U<<0)

void vcpu_inject_extint(struct vcpu *vcpu);
void vcpu_inject_nmi(struct vcpu *vcpu);
void vcpu_inject_gp(struct vcpu *vcpu, uint32_t err_code);
void vcpu_inject_pf(struct vcpu *vcpu, uint64_t addr, uint32_t err_code);
void vcpu_make_request(struct vcpu *vcpu, uint16_t eventid);
int vcpu_queue_exception(struct vcpu *vcpu, uint32_t vector, uint32_t err_code);

int exception_vmexit_handler(struct vcpu *vcpu);
int interrupt_window_vmexit_handler(struct vcpu *vcpu);
int external_interrupt_vmexit_handler(struct vcpu *vcpu);
int acrn_handle_pending_request(struct vcpu *vcpu);
void interrupt_init(uint16_t pcpu_id);

void cancel_event_injection(struct vcpu *vcpu);

#ifdef HV_DEBUG
void get_cpu_interrupt_info(char *str_arg, int str_max);
#endif /* HV_DEBUG */

#endif /* ARCH_IRQ_H */
