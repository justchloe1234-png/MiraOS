# MiraOS

MiraOS is a hands-on, from-scratch operating system project for x86_64 that aims to be understandable, extensible, and genuinely fun to build. Rather than relying on a heavy desktop environment or a large framework, it focuses on the core pieces that make an OS feel alive: bootstrapping, memory management, scheduling, drivers, filesystems, networking, and a simple user interface.

## What MiraOS includes

- a custom boot path with a small boot sector instead of GRUB
- a kernel that initializes the GDT, IDT, interrupts, paging, and a basic heap
- a simple process model and scheduler
- a virtual filesystem layer backed by a RAM filesystem
- basic graphics and input support for a lightweight UI shell
- a small set of kernel and user-facing primitives for experimentation

## Getting started

You will need:

- Zig
- NASM
- xorriso
- QEMU

On Windows, the included build script is the easiest entry point. On Linux or WSL, the Makefile should work as-is once the tools are installed.

```bash
# Build the image
make iso

# Build and launch in QEMU
make run

# Remove generated artifacts
make clean
```

## Project layout

```text
boot/
  boot.asm        early boot entry used by the kernel image
  stage1.asm      custom BIOS boot sector

arch/x86_64/
  cpu.h/c         low-level CPU helpers
  gdt.h/c         Global Descriptor Table setup
  idt.h/c         Interrupt Descriptor Table setup
  isr.h/c/asm     interrupt handling
  paging.h/c      paging support
  syscall_entry.asm  syscall entry

kernel/
  main.c          kernel entry point
  mem.h/c         physical memory manager
  heap.h/c        kernel heap allocator
  syscall.h/c     syscall dispatcher
  process.h/c     process and scheduler primitives
  elf.h/c         ELF loader
  panic.h/c       panic and diagnostic output

drivers/
  driver.h/c      driver subsystem
  framebuffer.h/c graphics framebuffer
  keyboard.h/c    keyboard input
  mouse.h/c       mouse input
  timer.h/c       timer support
  pic.h/c         interrupt controller setup
  ata.h/c         ATA storage driver

fs/
  vfs.h/c         virtual filesystem layer
  ramfs.h/c       RAM-backed filesystem

net/
  net.h/c         network stack core

ui/
  ui.h/c          UI framework and widget system
  shell.h/c       shell experience
  widget.h/c      widgets and panels
  gfx.h/c         graphics primitives
  text.h/c        text rendering
  input.h/c       input handling

lib/
  ds.h/c          data structure helpers
  cxxrt.h/c       runtime support
```

## Boot flow

The system now uses a lightweight custom boot path. The BIOS boot sector in the boot sector image prints a short message and then waits, while the kernel image is loaded directly by the emulator or boot medium. The kernel then initializes the core subsystems and enters its main loop.

## Development status

MiraOS is still a work in progress, but it already has a solid foundation for experimentation: memory management, interrupt handling, a basic scheduler, storage support, networking hooks, graphics, and a shell experience that can be extended over time.

## Contributing

Contributions are welcome. If you would like to help, please open an issue or a pull request, and take a moment to read the code of conduct before getting started.

## License

GPL 3.0