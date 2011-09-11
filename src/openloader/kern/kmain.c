/*
 *   openLoader C entry point.
 *   Copyright (C) 2011  Michel Megens
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <textio.h>
#include <stdlib.h>

#include <sys/io.h>
#include <sys/dev/ps2.h>
#include <sys/disk/ide.h>

#include <mm/mmap.h>
#include <mm/heap.h>

#include <interrupts/pic.h>
#include <interrupts/idt.h>

#include <sys/dev/pci.h>
#include <sys/disk/ide.h>
#include <sys/sys.h>

#include <arch/x86/cpu.h>
#include <arch/x86/apic/apic.h>
#include <arch/x86/acpi/acpi.h>

extern ol_lock_t lock;
void kmain(ol_mmap_register_t mmr)
{       
        ol_init_heap();
	textinit();
	clearscreen();

	println("The openLoader kernel is executing. \n");
        
        ol_cpu_t cpu = kalloc(sizeof(*cpu));
        ol_cpu_init(cpu);
        ol_get_system_tables();
        
	print("Current stack pointer: ");
	ol_registers_t regs = getregs();
	printnum(regs->esp, 16, FALSE, FALSE);
	putc(0xa);

	char status = inb(0x60);
	if((status & 2) == 2)
	{
		println("The A20 gate is open.");
		putc(0xa);
	}
	
	pic_init();
	setIDT();
        ol_ps2_init_keyboard();

// display mmap
	init_mmap(mmr);
	println("Multiboot memory map:\n");
	display_mmap(mmr);

        putc(0xa);
        ol_madt_apic_t* apics = ol_acpi_enumerate_apics();
        printnum(apics[0]->type, 16, FALSE, FALSE); putc(' ');
        printnum(apics[0]->flags, 16, FALSE, FALSE);
        free(apics);
        putc(0xa);
/*
        ol_pci_init();
*/
        
        
#if 0
        putc(0xa);
        
        struct partition_table bootdrive[4];
	uint8_t active = ide_init(bootdrive); /* search the boot partition */

        ol_ata_dev_t ata = kalloc(sizeof(struct ol_ata_dev));
        ata->base_port = 0x1f0;
        ata->dcr = 0x3f6;
        ata->slave = 0;
	uint8_t dev_type = ol_ata_detect_dev_type(ata);
	printnum(dev_type, 16, FALSE, FALSE);
        free(ata);
        putc(0xa);
#endif
        ol_apic_init(cpu);
	println("Waiting for service interrupts.. \n");

#ifdef __MEMTEST
        free(cpu);
        ol_detach_all_devices();
#endif
        
        printnum(*((uint32_t*)rsdp->signature), 16, 0, 0);
        putc(0x20);
        printnum(*(((uint32_t*)rsdp->signature)+1), 16, 0, 0);
        putc(0xa);
        
        ol_dbg_heap();

	while(1) halt();
	println("End of program reached!");
	endprogram();
}