.POSIX:

ISODIR := iso
MULTIBOOT := $(ISODIR)/boot/main.elf
MAIN := main.iso

.PHONY: clean run

$(MAIN):
	as -32 boot.S -o boot.o

	gcc -c kernel.cpp -ffreestanding -m32 -o kernel.o 

	gcc -c test.cpp -ffreestanding -m32 -o test.o 

	gcc -c test2.cpp -ffreestanding -m32 -o test2.o 

	gcc -ffreestanding -m32 -nostdlib -o '$(MULTIBOOT)' -T linker.ld boot.o kernel.o test.o test2.o -lgcc

	grub-mkrescue -o '$@' '$(ISODIR)'

clean:
	rm -f *.o '$(MULTIBOOT)' '$(MAIN)'

run: $(MAIN)
	qemu-system-i386 -cdrom '$(MAIN)'
	# Would also work.
	#qemu-system-i386 -hda '$(MAIN)'
	#qemu-system-i386 -kernel '$(MULTIBOOT)'
