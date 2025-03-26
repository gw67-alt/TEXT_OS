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

	gcc -c pci.c -ffreestanding -m32 -o pci.o -std=gnu99

	gcc -c io.c -ffreestanding -m32 -o io.o -std=gnu99

	gcc -ffreestanding -m32 -nostdlib -o '$(MULTIBOOT)' -T linker.ld boot.o kernel.o hardware_specs.o io.o pci.o stdio.o -lgcc -lc
	grub-mkrescue -o '$@' '$(ISODIR)'

clean:
	rm -f *.o '$(MULTIBOOT)' '$(MAIN)'