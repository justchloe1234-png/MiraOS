# MiraOS

MiraOS is a from scratch x86_64 operating system built for learning. It focuses on the core parts of an operating system including booting memory scheduling drivers filesystems networking and a minimal interface without relying on heavy frameworks or complex desktop systems.

## System capabilities

The project includes a lightweight custom boot sequence that skips GRUB entirely. The kernel sets up the GDT, IDT, interrupts, paging, and a simple heap allocator. It supports basic multitasking with a simple scheduler and process model, and uses a RAM based virtual filesystem for storage. It also includes basic input and display support for a minimal graphical shell and core development features.

## Directory structure

The boot folder contains the initial bootstrap code and custom BIOS sector. The arch folder holds hardware specific setup like CPU utilities, GDT and IDT configuration, paging, and system calls. The kernel directory handles core functions such as system entry, memory management, task scheduling, and ELF parsing. The drivers folder provides support for display, keyboard, mouse, timers, and storage. The fs folder implements the virtual filesystem and RAM disk, while the net folder contains networking hooks. The ui folder manages the interface, text engine, and shell, and the lib folder contains shared utilities and runtime helpers.

## Execution sequence

The OS uses a streamlined boot process where the BIOS sector starts execution, shows a brief message, and pauses while the kernel is loaded into memory by the host system or emulator. After loading, the kernel takes control, initializes core subsystems, and enters its main execution loop.

## Collaboration

Community input is welcome. To participate, you can submit an issue or open a pull request on the repository. Please review the project guidelines before submitting your code.

## Licensing

This software is distributed under the GPL 3.0 license.
