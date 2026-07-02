ZIG      ?= C:/Users/dchua/AppData/Local/Microsoft/WinGet/Packages/zig.zig_Microsoft.Winget.Source_8wekyb3d8bbwe/zig-x86_64-windows-0.16.0/zig.exe
ASM      ?= C:/msys64/usr/bin/nasm.exe
XORRISO  ?= C:/msys64/usr/bin/xorriso.exe


TARGET   = x86_64-freestanding-none
CC       = $(ZIG) cc -target $(TARGET)
LD       = $(ZIG) cc -target $(TARGET)

CFLAGS   = -ffreestanding -fno-stack-check -fno-stack-protector -mno-red-zone \
           -mcmodel=kernel -Wall -Wextra -Werror \
           -I. -Iarch/x86_64 -Idrivers -Idrivers/ps2 -Idrivers/storage -Ifs -Ifs/backend -Iui -Iui/layout -Iui/widgets -Ikernel -Ilib -Ilib/common -Inet -Inet/eth -O2

ASMFLAGS = -f elf64
LDFLAGS  = -nostdlib -Wl,-T,linker.ld -Wl,-z,max-page-size=0x1000

BOOT_ASM = boot/boot.asm
STAGE1_ASM = boot/stage1.asm
QEMU ?= qemu-system-x86_64

ASM_SRCS = arch/x86_64/isr.asm arch/x86_64/syscall_entry.asm

C_SRCS   = kernel/main.c \
           kernel/panic.c \
           kernel/mem.c \
           kernel/heap.c \
           kernel/syscall.c \
           kernel/process.c \
           kernel/elf.c \
           kernel/bin/mira_exec.c \
           kernel/bin/mira_bin_validate.c \
           kernel/bin/mira_bin_map.c \
           kernel/bin/mira_bin_checksum.c \
           arch/x86_64/gdt.c \
           arch/x86_64/idt.c \
           arch/x86_64/isr.c \
           arch/x86_64/paging.c \
           drivers/driver.c \
           drivers/timer.c \
           drivers/ps2/keyboard.c \
           drivers/framebuffer.c \
           drivers/ps2/mouse.c \
           drivers/pic.c \
           drivers/storage/ata.c \
           fs/vfs.c \
           fs/backend/ramfs.c \
           lib/common/ds.c \
           lib/common/mem.c \
           lib/common/string.c \
           lib/common/cxxrt.c \
           net/eth/net.c \
           ui/ui.c \
           ui/widgets/widget.c \
           ui/widgets/window.c \
           ui/layout/gfx.c \
           ui/layout/text.c \
           ui/input.c \
           ui/shell.c

BOOT_OBJ = build/boot.o
STAGE1_OBJ = build/stage1.bin
ASM_OBJS = build/arch/x86_64/isr_asm.o build/arch/x86_64/syscall_entry.o
C_OBJS   = $(C_SRCS:%.c=build/%.o)
OBJS     = $(BOOT_OBJ) $(ASM_OBJS) $(C_OBJS)

ISO      = miraos.iso
KERNEL   = build/kernel.elf

.PHONY: all iso clean dirs run

all: iso

iso: $(ISO)

$(ISO): $(KERNEL) build/stage1.bin
	rm -rf iso
	mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel.elf
	cp build/stage1.bin iso/boot/stage1.bin

	$(XORRISO) -return_with SORRY 0 -as mkisofs -R -J -joliet-long \
		-o $(ISO) iso

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

build/boot.o: boot/boot.asm | dirs
	$(ASM) $(ASMFLAGS) -o $@ $<

build/stage1.bin: boot/stage1.asm | dirs
	$(ASM) -f bin -o $@ $<

build/arch/x86_64/isr_asm.o: arch/x86_64/isr.asm | dirs
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) -o $@ $<

build/arch/x86_64/syscall_entry.o: arch/x86_64/syscall_entry.asm | dirs
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) -o $@ $<

build/%.o: %.c | dirs
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

dirs:
	@mkdir -p build/arch/x86_64 build/boot build/kernel build/kernel/bin \
	          build/drivers build/drivers/ps2 build/drivers/storage \
	          build/fs build/fs/backend \
	          build/ui build/ui/widgets build/ui/layout \
	          build/lib build/lib/common \
	          build/net build/net/eth

clean:
	rm -rf build iso $(ISO)

run: $(KERNEL)
	"$(QEMU)" -kernel $(KERNEL) -m 256M -serial stdio -vga std -no-reboot -no-shutdown

