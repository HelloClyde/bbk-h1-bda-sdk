.set noreorder
.text
.balign 64
.global assembly_helper
assembly_helper:
    jr $ra
    addiu $v0,$a0,1
