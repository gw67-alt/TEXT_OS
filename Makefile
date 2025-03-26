.POSIX:

ISODIR := iso
MULTIBOOT := $(ISODIR)/boot/main.elf
MAIN := main.iso

.PHONY: clean run

$(MAIN):
	as -32 boot.S -o boot.o
	
	gcc -c kernel.c -ffreestanding -m32 -o kernel.o -std=gnu99
	
	gcc -c hardware_specs.c -ffreestanding -m32 -o hardware_specs.o -std=gnu99

	gcc -c stdio.c -ffreestanding -m32 -o stdio.o -std=gnu99

	gcc -c nvmed_info_identify.c -ffreestanding -m32 -o nvmed_info_identify.o -std=gnu99


	gcc -c nvmed_info_utils.c -ffreestanding -m32 -o nvmed_info_utils.o -std=gnu99


	gcc -c lib_nvmed.c -ffreestanding -m32 -o lib_nvmed.o -std=gnu99


	gcc -c io.c -ffreestanding -m32 -o io.o -std=gnu99

	gcc -c pci.c -ffreestanding -m32 -o pci.o -std=gnu99
	gcc -c radix-tree.c -ffreestanding -m32 -o radix-tree.o -std=gnu99

	gcc -c nvmed_info_logs.c -ffreestanding -m32 -o nvmed_info_logs.o -std=gnu99

	gcc -c nvmed_info.c -ffreestanding -m32 -o nvmed_info.o -std=gnu99
	gcc -c nvmed_info_features.c -ffreestanding -m32 -o nvmed_info_features.o -std=gnu99


	gcc -c nvmed_info_pci.c -ffreestanding -m32 -o nvmed_info_pci.o -std=gnu99

	gcc -ffreestanding -m32 -nostdlib -o '$(MULTIBOOT)' -T linker.ld boot.o kernel.o  hardware_specs.o io.o pci.o stdio.o nvmed_info_logs.o nvmed_info_pci.o nvmed_info_features.o nvmed_info_identify.o nvmed_info.o nvmed_info_utils.o lib_nvmed.o radix-tree.o -lgcc -lc
	grub-mkrescue -o '$@' '$(ISODIR)'

clean:
	rm -f *.o '$(MULTIBOOT)' '$(MAIN)'