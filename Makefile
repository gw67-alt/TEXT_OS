.POSIX:

ISODIR := iso
MULTIBOOT := $(ISODIR)/boot/main.elf
MAIN := main.iso

.PHONY: clean run

$(MAIN):
	as -32 boot.S -o boot.o
	gcc -c kernel.c -ffreestanding -m32 -o kernel.o -std=gnu99
	gcc -c terminal.c -ffreestanding -m32 -o terminal.o -std=gnu99
	gcc -c hardware_specs.c -ffreestanding -m32 -o hardware_specs.o -std=gnu99
	gcc -ffreestanding -m32 -nostdlib -o '$(MULTIBOOT)' -T linker.ld boot.o kernel.o terminal.o hardware_specs.o -lgcc
	grub-mkrescue -o '$@' '$(ISODIR)'

clean:
	rm -f *.o '$(MULTIBOOT)' '$(MAIN)'

run: $(MAIN)
	qemu-system-i386 -cdrom '$(MAIN)'
	# Would also work.
	#qemu-system-i386 -hda '$(MAIN)'
	#qemu-system-i386 -kernel '$(MULTIBOOT)'
