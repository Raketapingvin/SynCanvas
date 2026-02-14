global long_mode_start
extern kernel_main

section .text
bits 64
long_mode_start:
    ; load null into all data segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; Ensure the Multiboot pointer (saved in edi) is zero-extended to rdi
    mov edi, edi

    call kernel_main
    hlt
