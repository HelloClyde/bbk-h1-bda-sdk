    .set noreorder

    /* The BDA loader copies the file payload and can leave following RAM intact. */
    .section .data.h1_power_context,"aw",@progbits
    .align 2
h1_power_context:
    .word 0

    .section .text.h1_power_run,"ax",@progbits
    .globl h1_power_run
    .type h1_power_run,@function
h1_power_run:
    addiu $sp,$sp,-64
    /* O32 reserves 16 bytes for the callee's argument slots. */
    sw $s0,16($sp)
    sw $s1,20($sp)
    sw $s2,24($sp)
    sw $s3,28($sp)
    sw $s4,32($sp)
    sw $s5,36($sp)
    sw $s6,40($sp)
    sw $s7,44($sp)
    sw $fp,48($sp)
    sw $gp,52($sp)
    sw $ra,56($sp)
    lui $t0,%hi(h1_power_context)
    lw $t1,%lo(h1_power_context)($t0)
    sw $t1,60($sp)
    jalr $a0
    sw $sp,%lo(h1_power_context)($t0)
    move $v0,$zero
.Lpower_return:
    lui $t0,%hi(h1_power_context)
    lw $t1,60($sp)
    sw $t1,%lo(h1_power_context)($t0)
    lw $s0,16($sp)
    lw $s1,20($sp)
    lw $s2,24($sp)
    lw $s3,28($sp)
    lw $s4,32($sp)
    lw $s5,36($sp)
    lw $s6,40($sp)
    lw $s7,44($sp)
    lw $fp,48($sp)
    lw $gp,52($sp)
    lw $ra,56($sp)
    jr $ra
    addiu $sp,$sp,64
    .size h1_power_run,.-h1_power_run

    .globl h1_power_unwind
    .hidden h1_power_unwind
    .type h1_power_unwind,@function
h1_power_unwind:
    lui $t0,%hi(h1_power_context)
    lw $t0,%lo(h1_power_context)($t0)
    beqz $t0,.Lno_scope
    addiu $v0,$zero,1
    b .Lpower_return
    move $sp,$t0
.Lno_scope:
    jr $ra
    nop
    .size h1_power_unwind,.-h1_power_unwind
