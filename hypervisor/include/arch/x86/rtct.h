/*
 * Copyright (C) 2020 Intel Corporation. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RTCT_H
#define RTCT_H

#include <acpi.h>


#define RTCT_ENTRY_TYPE_RTCD_LIMIT		1U
#define RTCT_ENTRY_TYPE_RTCM_BINARY		2U
#define RTCT_ENTRY_TYPE_WRC_L3_MASKS		3U
#define RTCT_ENTRY_TYPE_GT_L3_MASKS		4U
#define RTCT_ENTRY_TYPE_SOFTWARE_SRAM		5U
#define RTCT_ENTRY_TYPE_STREAM_DATAPATH		6U
#define RTCT_ENTRY_TYPE_TIMEAWARE_SUBSYS	7U
#define RTCT_ENTRY_TYPE_RT_IOMMU		8U
#define RTCT_ENTRY_TYPE_MEM_HIERARCHY_LATENCY	9U

#define SOFTWARE_SRAM_BASE_HPA 0x40080000U
#define SOFTWARE_SRAM_BASE_GPA 0x40080000U
#define SOFTWARE_SRAM_MAX_SIZE 0x00800000U

struct rtct_entry {
	 uint16_t size;
	 uint16_t format;
	 uint32_t type;
	 uint32_t data[64];
} __packed;

struct rtct_entry_data_rtcm_binary
{
	uint64_t address;
	uint32_t size;
} __packed;

struct rtct_entry_data_software_sram
{
	uint32_t cache_level;
	uint64_t base;
	uint32_t ways;
	uint32_t size;
	uint32_t apic_id_0; /*only the first core is responsible for initialization of L3 mem region*/
} __packed;


extern uint64_t software_sram_area_bottom;
extern uint64_t software_sram_area_top;

#endif /* RTCT_H */
