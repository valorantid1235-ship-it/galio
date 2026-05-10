# Simple build for a 32-bit freestanding kernel
CC = gcc
AS = nasm
LD = ld

CFLAGS = -m32 -ffreestanding -O2 -Wall -Wextra -Iinclude -Itools/shell
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T boot/linker.ld

SRCS = src/kernel.c src/kmain.c src/vga.c src/gdt.c src/idt.c src/irq.c src/isr.c src/kprintf.c \
       src/serial.c src/pmem.c src/paging.c src/heap.c src/pit.c src/keyboard.c src/process.c \
       src/syscall.c src/elf.c src/vfs.c src/string.c src/mem_test.c src/auth.c tools/shell/shell.c
OBJS = $(SRCS:.c=.o) src/asm.o src/isr_asm.o boot/boot.o src/embedded_test.o src/embedded_initrd.o
TEST_ELF = test_elf.bin
INITRD_IMAGE = initrd.bin

src/embedded_test.o: $(TEST_ELF)
	objcopy -I binary -O elf32-i386 -B i386 $< $@

test_elf.bin: test/test_elf.c
	gcc -m32 -ffreestanding -nostdlib -Wl,--entry=_start -Wl,-Ttext=0x10000 $< -o $@

tools/mkiofs: tools/mkiofs.c
	gcc -o $@ $<

$(INITRD_IMAGE): tools/mkiofs
	./tools/mkiofs $@

src/embedded_initrd.o: $(INITRD_IMAGE)
	objcopy -I binary -O elf32-i386 -B i386 $< $@

.PHONY: all clean run

all: galio.bin

galio.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

boot/boot.o: boot/boot.S
	$(CC) $(CFLAGS) -c boot/boot.S -o boot/boot.o

src/asm.o: src/asm.s
	$(AS) $(ASFLAGS) src/asm.s -o src/asm.o

src/isr_asm.o: src/isr_asm.s
	$(AS) $(ASFLAGS) src/isr_asm.s -o src/isr_asm.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f galio.bin $(OBJS) tools/mkiofs $(INITRD_IMAGE)

run: galio.bin
	qemu-system-i386 -kernel galio.bin -m 128M -serial stdio