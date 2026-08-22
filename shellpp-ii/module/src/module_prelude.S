.syntax unified
.thumb

/* Keep module_initialize at a nonzero Thumb offset within the standard .text. */
.section .text, "ax", %progbits
.p2align 1
.type shellpp_ii_module_prelude, %function
shellpp_ii_module_prelude:
    bx lr
.size shellpp_ii_module_prelude, . - shellpp_ii_module_prelude

/* s441 modlib calls the exported value directly. Keep this entry self
 * contained so the module has no entry-point branch relocation. */
.global module_initialize
.type module_initialize, %function
.thumb_func
module_initialize:
    cmp r0, #0
    beq 1f
    movs r1, #0
    str r1, [r0, #0]
    str r1, [r0, #4]
    str r1, [r0, #8]
    str r1, [r0, #12]
    movs r0, #0
    bx lr
1:
    movs r0, #22
    rsbs r0, r0, #0
    bx lr
.size module_initialize, . - module_initialize
