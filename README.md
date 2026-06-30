# MiraOS

MiraOS is an operating system built from scratch for x86_64. It's written mostly in C with some Assembly where it's actually needed, like boot code and low level CPU stuff. The goal is to understand how an OS works at the lowest level by writing the kernel, drivers, and filesystem myself instead of relying on existing frameworks.

It boots through GRUB using a custom linker script and bootloader config, then hands off to a kernel that handles the core of the system. From there it has its own drivers for talking to hardware, a basic filesystem layer for reading and writing data, and a simple UI on top.

It's a place to dig into kernel development, memory management, drivers, and all the stuff that normally gets hidden away by an OS you didn't build yourself.

## Building

The project uses a Makefile and a Dockerfile so the build environment stays consistent no matter what machine you're building on. There's also a build-iso script for Windows users who want to generate a bootable ISO without setting up a full Linux toolchain.

## Status

Still early and actively evolving. Expect things to be incomplete, broken, or rewritten without warning.

## License

GPL-3.0
