# Copyright (C) 2019 Intel Corporation. All rights reserved.
#
# SPDX-License-Identifier: BSD-3-Clause
#

import sys
import subprocess
import board_cfg_lib
import hv_cfg_lib
import common


DESC = """# Board defconfig generated by acrn-config tool
"""

VM_NUM_MAP_TOTAL_HV_RAM_SIZE = {
    # 120M
    2:0x7800000,
    # 150M
    3:0x9600000,
    # 180M
    4:0xB400000,
    # 210M
    5:0xD200000,
    # 250M
    6:0xFA00000,
    # 280M
    7:0x11800000,
    # 320M
    8:0x14000000,
}

MEM_ALIGN = 2 * common.SIZE_M


def find_avl_memory(ram_range, hpa_size, hv_start_offset):
    """
    This is get hv address from System RAM as host physical size
    :param ram_range: System RAM mapping
    :param hpa_size: fixed host physical size
    :param hv_start_offset: base address of HV RAM start
    :return: start host physical address
    """
    ret_start_addr = 0
    tmp_order_key = 0

    tmp_order_key = sorted(ram_range)
    for start_addr in tmp_order_key:
        mem_range = ram_range[start_addr]
        if start_addr < hv_start_offset < start_addr +  ram_range[start_addr]:
            # 256M address located in this start ram range
            if start_addr + mem_range - hv_start_offset > int(hpa_size, 10):
                ret_start_addr = hv_start_offset
                break
        elif start_addr > hv_start_offset:
            # above 256M address, than return the start address of this ram range
            ret_start_addr = start_addr
            break

    return hex(ret_start_addr)


def get_ram_range():
    """ Get System RAM range mapping """
    # read system ram from board_info.xml
    ram_range = {}

    io_mem_lines = board_cfg_lib.get_info(
        common.BOARD_INFO_FILE, "<IOMEM_INFO>", "</IOMEM_INFO>")

    for line in io_mem_lines:
        if 'System RAM' not in line:
            continue
        start_addr = int(line.split('-')[0], 16)
        end_addr = int(line.split('-')[1].split(':')[0], 16)
        mem_range = end_addr - start_addr
        ram_range[start_addr] = mem_range

    return ram_range


def get_serial_type():
    """ Get serial console type specified by user """
    ttys_type = ''
    ttys_value = ''

    # Get ttySx information from board config file
    ttys_lines = board_cfg_lib.get_info(common.BOARD_INFO_FILE, "<TTYS_INFO>", "</TTYS_INFO>")

    # Get ttySx from scenario config file which selected by user
    (err_dic, ttyn) = board_cfg_lib.parser_vuart_console()
    if err_dic:
        hv_cfg_lib.ERR_LIST.update(err_dic)

    # query the serial type from board config file
    for line in ttys_lines:
        if ttyn in line:
            # line format:
            # seri:/dev/ttyS0 type:portio base:0x3F8 irq:4
            # seri:/dev/ttyS0 type:mmio base:0xB3640000 irq:4 bdf:"0:x.y"
            ttys_type = line.split()[1].split(':')[1]
            if ttys_type == "portio":
                ttys_value = line.split()[2].split(':')[1]
            elif ttys_type == "mmio":
                ttys_value = line.split()[-1].split('"')[1:-1][0]
            break

    return (ttys_type, ttys_value)


def is_rdt_supported():
    """
    Returns True if platform supports RDT else False
    """
    (rdt_resources, rdt_res_clos_max, _) = board_cfg_lib.clos_info_parser(common.BOARD_INFO_FILE)
    if len(rdt_resources) == 0 or len(rdt_res_clos_max) == 0:
        return False
    else:
        return True


def get_memory(hv_info, config):

    # this dictonary mapped with 'address start':'mem range'
    ram_range = {}

    if common.VM_COUNT in list(VM_NUM_MAP_TOTAL_HV_RAM_SIZE.keys()):
        hv_ram_size = VM_NUM_MAP_TOTAL_HV_RAM_SIZE[common.VM_COUNT]
    else:
        common.print_red("VM num should not be greater than 8", err=True)
        err_dic["board config: total vm number error"] = "VM num should not be greater than 8"
        return err_dic

    ram_range = get_ram_range()

    # reseve 16M memory for hv sbuf, ramoops, etc.
    reserved_ram = 0x1000000
    # We recommend to put hv ram start address high than 0x10000000 to
    # reduce memory conflict with GRUB/SOS Kernel.
    hv_start_offset = 0x10000000
    total_size = reserved_ram + hv_ram_size
    avl_start_addr = find_avl_memory(ram_range, str(total_size), hv_start_offset)
    hv_start_addr = int(avl_start_addr, 16) + int(hex(reserved_ram), 16)
    hv_start_addr = common.round_up(hv_start_addr, MEM_ALIGN)

    if not hv_info.mem.hv_ram_start:
        print("CONFIG_HV_RAM_START={}".format(hex(hv_start_addr)), file=config)
    else:
        print("CONFIG_HV_RAM_START={}".format(hv_info.mem.hv_ram_start), file=config)
    if not hv_info.mem.hv_ram_size:
        print("CONFIG_HV_RAM_SIZE={}".format(hex(hv_ram_size)), file=config)
    else:
        print("CONFIG_HV_RAM_SIZE={}".format(hv_info.mem.hv_ram_size), file=config)

    print("CONFIG_PLATFORM_RAM_SIZE={}".format(hv_info.mem.platform_ram_size), file=config)
    print("CONFIG_LOW_RAM_SIZE={}".format(hv_info.mem.low_ram_size), file=config)
    print("CONFIG_SOS_RAM_SIZE={}".format(hv_info.mem.sos_ram_size), file=config)
    print("CONFIG_UOS_RAM_SIZE={}".format(hv_info.mem.uos_ram_size), file=config)
    print("CONFIG_STACK_SIZE={}".format(hv_info.mem.stack_size), file=config)


def get_serial_console(config):

    (serial_type, serial_value) = get_serial_type()
    if serial_type == "portio":
        print("CONFIG_SERIAL_LEGACY=y", file=config)
        print("CONFIG_SERIAL_PIO_BASE={}".format(serial_value), file=config)
    if serial_type == "mmio":
        print("CONFIG_SERIAL_PCI=y", file=config)
        print('CONFIG_SERIAL_PCI_BDF="{}"'.format(serial_value), file=config)


def get_miscfg(hv_info, config):

    print("CONFIG_GPU_SBDF={}".format(hv_info.mis.gpu_sbdf), file=config)
    print('CONFIG_UEFI_OS_LOADER_NAME="{}"'.format(hv_info.mis.uefi_os_loader_name), file=config)


def get_features(hv_info, config):

    print("CONFIG_{}=y".format(hv_info.features.scheduler), file=config)
    print("CONFIG_RELOC={}".format(hv_info.features.reloc), file=config)
    print("CONFIG_MULTIBOOT2={}".format(hv_info.features.multiboot2), file=config)
    print("CONFIG_HYPERV_ENABLED={}".format(hv_info.features.hyperv_enabled), file=config)
    print("CONFIG_IOMMU_ENFORCE_SNP={}".format(hv_info.features.iommu_enforce_snp), file=config)
    print("CONFIG_ACPI_PARSE_ENABLED={}".format(hv_info.features.acpi_parse_enabled), file=config)
    print("CONFIG_L1D_FLUSH_VMENTRY_ENABLED={}".format(hv_info.features.l1d_flush_vmentry_enabled), file=config)
    print("CONFIG_MCE_ON_PSC_WORKAROUND_DISABLED={}".format(hv_info.features.mce_on_psc_workaround_disabled), file=config)


def get_capacities(hv_info, config):

    print("CONFIG_IOMMU_BUS_NUM={}".format(hv_info.cap.iommu_bus_num), file=config)
    print("CONFIG_MAX_IOAPIC_NUM={}".format(hv_info.cap.max_ioapic_num), file=config)
    print("CONFIG_MAX_IR_ENTRIES={}".format(hv_info.cap.max_ir_entries), file=config)
    print("CONFIG_MAX_PCI_DEV_NUM={}".format(hv_info.cap.max_pci_dev_num), file=config)
    print("CONFIG_MAX_IOAPIC_LINES={}".format(hv_info.cap.max_ioapic_lines), file=config)
    print("CONFIG_MAX_PT_IRQ_ENTRIES={}".format(hv_info.cap.max_pt_irq_entries), file=config)
    print("CONFIG_MAX_MSIX_TABLE_NUM={}".format(hv_info.cap.max_msix_table_num), file=config)
    print("CONFIG_MAX_EMULATED_MMIO_REGIONS={}".format(hv_info.cap.max_emu_mmio_regions), file=config)


def get_log_opt(hv_info, config):

    print("CONFIG_LOG_BUF_SIZE={}".format(hv_info.log.buf_size), file=config)
    print("CONFIG_NPK_LOGLEVEL={}".format(hv_info.log.level.npk), file=config)
    print("CONFIG_MEM_LOGLEVEL={}".format(hv_info.log.level.mem), file=config)
    print("CONFIG_LOG_DESTINATION={}".format(hv_info.log.dest), file=config)
    print("CONFIG_CONSOLE_LOGLEVEL={}".format(hv_info.log.level.console), file=config)


def generate_file(hv_info, config):
    """Start to generate board.c
    :param config: it is a file pointer of board information for writing to
    """
    err_dic = {}

    # add config scenario name
    (err_dic, scenario_name) = common.get_scenario_name()
    (err_dic, board_name) = common.get_board_name()

    print("{}".format(DESC), file=config)
    if hv_info.log.release == 'y':
        print("CONFIG_RELEASE=y", file=config)
    print('CONFIG_BOARD="{}"'.format(board_name), file=config)

    get_memory(hv_info, config)
    get_miscfg(hv_info, config)
    get_features(hv_info, config)
    get_capacities(hv_info, config)
    get_serial_console(config)
    get_log_opt(hv_info, config)

    if is_rdt_supported():
        print("CONFIG_RDT_ENABLED=y", file=config)
    else:
        print("CONFIG_RDT_ENABLED=n", file=config)

    print("CONFIG_ENFORCE_VALIDATED_ACPI_INFO=y", file=config)

    return err_dic
