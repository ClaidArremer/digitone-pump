| SPDX-License-Identifier: GPL-2.0-or-later
| PUMP's row in SETTINGS > GLOBAL FX/MIX (core 4.1's ev_fxmix): PUMP ANIMATION,
| a checkbox. Ticked (the default), the PUMP page animates (the turning
| record, the graph, the meter); unticked, it stays still and draws twice a
| second. The row's four callbacks follow the menu's MenuItem conventions as
| digihealth's SYSTEM INFO row does (irpina/digihealth, sysinfo.s,
| GPL-2.0-or-later), from which they are adapted, with the Digitakt mk1's
| string constructor and checkbox bitmaps (as Digi EQ's MASTER EQ row finds
| them). The choice is pumpctl_anim_off byte 0: 0 animate, 1 still.

        .ifdef  OS154                   | the Digitakt mk1 1.54 (mod.json's port)
        .equ STR_CTOR,    0x4017af20    | std::string(this, const char*, alloc&)
        .equ CHECKBOXES,  0x421f8a3c    | -> Bitmap[2], 0x1c each: empty, ticked
        .else                           | 1.53
        .equ STR_CTOR,    0x4017ac20
        .equ CHECKBOXES,  0x421f7a3c
        .endif

        .section .run, "ax"

| ev_fxmix handler: the row, after GLOBAL FX/MIX's own
        .globl  pumpdt_fxmix
pumpdt_fxmix:
        pea     ps_row
        move.l  8(%sp), -(%sp)          | the menu
        jsr     core_additem
        addq.l  #8, %sp
        rts

| label() -> std::string, returned through a0
ps_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        pea     -1(%a6)
        pea     ps_str
        move.l  %a0, %d2
        move.l  %a0, -(%sp)
        jsr     STR_CTOR
        lea     12(%sp), %sp
        move.l  %d2, %d0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts

| select(payload, item): toggle, then redraw the menu
ps_select:
        movea.l 4(%sp), %a0
        movea.l (%a0), %a0
        moveq   #0, %d0
        tst.b   pumpctl_anim_off
        seq     %d0
        andi.l  #1, %d0
        move.b  %d0, pumpctl_anim_off
        lea     0x38(%a0), %a0
        move.l  %a0, 4(%sp)
        jmp     fw_invalidate

| change(payload, item, delta): RIGHT ticks it (animate), LEFT unticks
ps_change:
        movea.l 4(%sp), %a0
        movea.l (%a0), %a0
        move.l  12(%sp), %d0
        beq.s   1f
        sle     %d0                     | delta <= 0: still (1); > 0: animate (0)
        andi.l  #1, %d0
        move.b  %d0, pumpctl_anim_off
        lea     0x38(%a0), %a0
        move.l  %a0, 4(%sp)
        jmp     fw_invalidate
1:      rts

| draw(payload, item, bmp, x, y): the checkbox, ticked while animating
ps_draw:
        moveq   #0, %d0
        tst.b   pumpctl_anim_off
        bne.s   1f
        moveq   #0x1c, %d0
1:      add.l   CHECKBOXES, %d0
        move.l  12(%sp), %d1
        move.l  %d1, 4(%sp)
        move.l  %d0, 8(%sp)
        move.l  16(%sp), %d1
        move.l  %d1, 12(%sp)
        move.l  20(%sp), %d1
        move.l  %d1, 16(%sp)
        clr.l   20(%sp)
        jmp     fw_blit

        .balign 4
ps_row: .long   ps_label, ps_select, ps_draw, ps_change
ps_str: .asciz  "PUMP ANIMATION"
        .balign 4
