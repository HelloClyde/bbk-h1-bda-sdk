.set noreorder
.section .text.h1_bda_entry,"ax",@progbits
.balign 4
.global h1_bda_main
.type h1_bda_main,@function
h1_bda_main:
    addiu $sp,$sp,-32
    sw $ra,28($sp)
    sw $s0,24($sp)
    sw $gp,20($sp)
    move $s0,$sp
    lui $sp,%hi(__h1_app_stack_end)
    addiu $sp,$sp,%lo(__h1_app_stack_end)
    lui $gp,%hi(_gp)
    addiu $gp,$gp,%lo(_gp)
    jal h1_runtime_start
    nop
    move $sp,$s0
    lw $gp,20($sp)
    lw $s0,24($sp)
    lw $ra,28($sp)
    jr $ra
    addiu $sp,$sp,32
.size h1_bda_main,.-h1_bda_main

.section .h1_noinit.runtime_stack,"aw",@nobits
.balign 16
.space 131072
.global __h1_app_stack_end
__h1_app_stack_end:
