/*
 * Copyright (C) 2018 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <vm_config.h>
#include <vuart.h>
#include <pci_dev.h>

struct acrn_vm_config vm_configs[CONFIG_MAX_VM_NUM] = {
	{	/* VM0 */
		CONFIG_SOS_VM,
		.name = "ACRN SOS VM",

		/* Allow SOS to reboot the host since there is supposed to be the highest severity guest */
		.guest_flags = 0UL,
		.memory = {
			.start_hpa = 0UL,
			.size = CONFIG_SOS_RAM_SIZE,
		},
		.os_config = {
			.name = "ACRN Service OS",
			.kernel_type = KERNEL_BZIMAGE,
			.kernel_mod_tag = "Linux_bzImage",
			.bootargs = SOS_VM_BOOTARGS,
		},
		.vuart[0] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = SOS_COM1_BASE,
			.irq = SOS_COM1_IRQ,
		},
		.vuart[1] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = INVALID_COM_BASE,
		},
		.pci_dev_num = SOS_EMULATED_PCI_DEV_NUM,
		.pci_devs = sos_pci_devs,
	},
	{	/* VM1 */
		CONFIG_POST_STD_VM(1),
		.vcpu_num = 1U,
		.vcpu_affinity = VM1_CONFIG_VCPU_AFFINITY,
		.vuart[0] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = INVALID_COM_BASE,
		},
		.vuart[1] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = INVALID_COM_BASE,
		}

	},
	{	/* VM2 */
		CONFIG_POST_STD_VM(2),
		.vcpu_num = 1U,
		.vcpu_affinity = VM2_CONFIG_VCPU_AFFINITY,
		.vuart[0] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = INVALID_COM_BASE,
		},
		.vuart[1] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = INVALID_COM_BASE,
		}

	},
	{	/* VM3 */
		CONFIG_POST_STD_VM(3),
		.vcpu_num = 1U,
		.vcpu_affinity = VM3_CONFIG_VCPU_AFFINITY,
		.vuart[0] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = INVALID_COM_BASE,
		},
		.vuart[1] = {
			.type = VUART_LEGACY_PIO,
			.addr.port_base = INVALID_COM_BASE,
		}

	}
};
