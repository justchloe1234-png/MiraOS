@echo off
setlocal
set "QEMU="
for %%I in (qemu-system-x86_64.exe qemu-system-x86_64 qemu.exe) do (
    where %%I >nul 2>nul
    if not errorlevel 1 (
        for /f "delims=" %%P in ('where %%I 2^>nul') do (
            set "QEMU=%%P"
            goto :found
        )
    )
)
if not defined QEMU (
    echo QEMU was not found on PATH. Install QEMU and try again.
    exit /b 1
)
:found
"%QEMU%" -kernel build\kernel.elf -m 256M -nographic -serial stdio -monitor none -no-reboot -no-shutdown
