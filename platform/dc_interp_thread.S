/* Experimental SH4 threaded integer interpreter. No generated-code cache.
 * r8=guest PC, r9=current word, r10=GPRs, r11=RDRAM, r12=dispatch,
 * r13=original-handler callback, r14=stop. Save all callee-saved integer
 * registers and PR; no floating register or FPSCR instruction is used.
 * Original handlers own branches, delay slots, FPU, exceptions and devices.
 */
    .text
    .align 2
    .global _dc_interp_run_thread
    .type _dc_interp_run_thread,@function
_dc_interp_run_thread:
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    sts.l pr,@-r15
    mov.l .Lpc,r0
    mov.l @r0,r8
    mov.l .Lop,r0
    mov.l @r0,r9
    mov.l .Lreg,r10
    mov.l .Lram,r11
    mov.l .Ltable,r12
    mov.l .Lcallback,r13
    mov.l .Lstop,r14
.Lnext:
    mov.l @r14,r0
    tst r0,r0
    bt .Lbudget
    bra .Lfinished
    nop
.Lbudget:
    mov.l .Llimit,r0
    mov.l @r0,r1
    tst r1,r1
    bt .Lfetch
    mov.l .Lsteps,r2
    mov.l @r2,r0
    add #1,r0
    mov.l r0,@r2
    cmp/hs r1,r0
    bf .Lfetch
    mov #1,r0
    mov.l r0,@r14
    bra .Lfinished
    nop
.Lfetch:
    mov r8,r0
    mov.l .Laliasmask,r1
    and r1,r0
    mov.l .Lkseg,r1
    cmp/eq r1,r0
    bt .Lramfetch
    bra .Loriginal
    nop
.Lramfetch:
    mov r8,r0
    mov.l .Lwordmask,r1
    and r1,r0
    add r11,r0
    mov.l @r0,r9
    mov r9,r0
    shlr16 r0
    shlr8 r0
    shlr2 r0
    shll2 r0
    mov.l @(r0,r12),r0
    jmp @r0
    nop
.Loriginal:
    /* Publish the preceding opcode even when a translated fetch fails. */
    mov.l .Lop,r0
    mov.l r9,@r0
    mov r8,r4
    jsr @r13
    nop
    mov r0,r8
    mov.l .Lop,r0
    mov.l @r0,r9
    bra .Lnext
    nop
.Lfinished:
    mov.l .Lpc,r0
    mov.l r8,@r0
    mov.l .Lop,r0
    mov.l r9,@r0
    lds.l @r15+,pr
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    rts
    mov.l @r15+,r8
    .align 2
.Lpc: .long _interp_addr
.Lop: .long _op
.Lreg: .long _reg
.Lram: .long _rdram
.Ltable: .long .Ldispatch
.Lcallback: .long _dc_interp_thread_fallback
.Lstop: .long _stop
.Llimit: .long _dc_interp_step_limit
.Lsteps: .long _dc_interp_steps
.Laliasmask: .long 0xdfc00000
.Lkseg: .long 0x80000000
.Lwordmask: .long 0x003ffffc

    /* The macros use r0 only to form a pointer. Their result is low/high
     * source r1/r2 and destination r3; source/destination aliases are safe. */
    .macro RS
    mov r9,r0
    shlr16 r0
    shlr2 r0
    and #248,r0
    add r10,r0
    .endm
    .macro RT
    mov r9,r0
    shlr16 r0
    shll2 r0
    shll r0
    and #248,r0
    add r10,r0
    .endm
    .macro RD
    mov r9,r0
    shlr8 r0
    and #248,r0
    add r10,r0
    .endm
    .macro SIGN32
    mov r1,r2
    mov #-31,r0
    shad r0,r2
    .endm
.Laddiu:
    RS
    mov.l @r0,r1
    mov r9,r2
    exts.w r2,r2
    add r2,r1
    SIGN32
    RT
    bra .Lstore64
    mov r0,r3
.Llui:
    mov r9,r1
    shll16 r1
    SIGN32
    RT
    bra .Lstore64
    mov r0,r3
.Landi:
    RS
    mov.l @r0,r1
    mov r9,r2
    extu.w r2,r2
    and r2,r1
    mov #0,r2
    RT
    bra .Lstore64
    mov r0,r3
.Lori:
    RS
    mov.l @r0,r1
    mov.l @(4,r0),r2
    mov r9,r3
    extu.w r3,r3
    or r3,r1
    RT
    bra .Lstore64
    mov r0,r3
.Lspecial:
    mov r9,r0
    and #63,r0
    tst r0,r0
    bt .Lsll
    cmp/eq #33,r0
    bt .Laddu
    cmp/eq #37,r0
    bt .Lor
    bra .Loriginal
    nop
.Lsll:
    RT
    mov.l @r0,r1
    mov r9,r0
    shlr2 r0
    shlr2 r0
    shlr2 r0
    and #31,r0
    shld r0,r1
    SIGN32
    RD
    bra .Lstore64
    mov r0,r3
.Laddu:
    RS
    mov.l @r0,r1
    RT
    mov.l @r0,r2
    add r2,r1
    SIGN32
    RD
    bra .Lstore64
    mov r0,r3
.Lor:
    RS
    mov.l @r0,r1
    mov.l @(4,r0),r2
    RT
    mov.l @r0,r3
    mov.l @(4,r0),r4
    or r3,r1
    or r4,r2
    RD
    bra .Lstore64
    mov r0,r3
.Lstore64:
    mov.l r1,@r3
    mov.l r2,@(4,r3)
.Lretired:
    add #4,r8
    bra .Lnext
    nop

.Llw:
    mov #0,r5
    bra .Lmemory
    nop
.Lsw:
    mov #16,r5
.Lmemory:
    RS
    mov.l @r0,r1
    mov r9,r2
    exts.w r2,r2
    add r2,r1
    mov r1,r0
    tst #3,r0
    bt .Laligned
    bra .Loriginal
    nop
.Laligned:
    mov.l .Lmemmask,r2
    and r1,r2
    mov.l .Lmemkseg,r3
    cmp/eq r3,r2
    bt .Lhandler
    bra .Loriginal
    nop
.Lhandler:
    mov r1,r0
    shlr16 r0
    shll2 r0
    mov.l .Lrwmem,r2
    mov.l @(r0,r2),r2
    add r5,r2
    mov.l @r2,r2
    mov.l .Lordinaryread,r3
    tst r5,r5
    bt .Lcheckhandler
    mov.l .Lordinarywrite,r3
.Lcheckhandler:
    cmp/eq r3,r2
    bt .Lordinary
    bra .Loriginal
    nop
.Lordinary:
    mov.l .Laddress,r0
    mov.l r1,@r0
    mov.l .Ldatamask,r2
    and r1,r2
    add r11,r2
    tst r5,r5
    bf .Lwriteword
    mov.l @r2,r1
    mov.l .Lword,r0
    mov.l r1,@r0
    SIGN32
    RT
    bra .Lstore64
    mov r0,r3
.Lwriteword:
    mov r2,r3
    RT
    mov.l @r0,r1
    mov.l r1,@r3
    mov.l .Lword,r0
    mov.l r1,@r0
    bra .Lretired
    nop
    .align 2
.Lmemmask: .long 0xdfc00000
.Lmemkseg: .long 0x80000000
.Ldatamask: .long 0x003fffff
.Lrwmem: .long _rwmem
.Lordinaryread: .long _read_rdram
.Lordinarywrite: .long _write_rdram
.Laddress: .long _address
.Lword: .long _word

    .section .rodata
    .align 2
.Ldispatch:
    .long .Lspecial,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal
    .long .Loriginal,.Laddiu,.Loriginal,.Loriginal,.Landi,.Lori,.Loriginal,.Llui
    .rept 2
    .long .Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal
    .endr
    .long .Loriginal,.Loriginal,.Loriginal,.Llw,.Loriginal,.Loriginal,.Loriginal,.Loriginal
    .long .Loriginal,.Loriginal,.Loriginal,.Lsw,.Loriginal,.Loriginal,.Loriginal,.Loriginal
    .rept 2
    .long .Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal,.Loriginal
    .endr
    .section .note.GNU-stack,"",@progbits
