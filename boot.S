.section .text
.align 4
# Multiboot header at the very start of .text (within first 8 KiB)
.long 0x1BADB002          # MAGIC
.long 0                    # FLAGS
.long -(0x1BADB002 + 0)    # CHECKSUM

# Simple 4 KiB stack
.section .bss
.align 16
stack_area:
    .skip 4096
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp
    call KERNEL_MAIN
    cli
1:  hlt
    jmp 1b
.size _start, . - _start

# Silence exec-stack warning
.section .note.GNU-stack,"",@progbits