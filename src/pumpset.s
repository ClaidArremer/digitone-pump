| SPDX-License-Identifier: GPL-2.0-or-later
| PUMP's SETTINGS row: PUMP ANIMATION, a checkbox. Ticked (the default), the
| PUMP page animates (the turning record, the graph); unticked, it stays
| still and draws twice a second. The row's four callbacks follow the
| SETTINGS menu's MenuItem conventions as digihealth's SYSTEM INFO row does
| (irpina/digihealth, sysinfo.s, GPL-2.0-or-later), from which they are
| adapted. The choice lives in pumpctl_anim_off (byte 0: 0 animate, 1 still),
| which pumpctl.c saves with the project.

        .ifdef  DN144                   | the Digitone mk1 1.44 (mod.json's port)
        .equ STR_CTOR,    0x4018afc8    | std::string(this, const char*, alloc&)
        .equ INVALIDATE,  0x400e539e    | View::invalidate(view)
        .equ BLIT,        0x400de4c0    | blit(dst, src, x, y, centre)
        .equ CHECKBOXES,  0x419ee988    | -> Bitmap[2], 0x1c each: empty, ticked
        .else                           | 1.43
        .equ STR_CTOR,    0x4018ac7c
        .equ INVALIDATE,  0x400e512a
        .equ BLIT,        0x400de24c
        .equ CHECKBOXES,  0x419ed988
        .endif

        .section .run, "ax"

| ev_settings handler: the row, after the stock ones (and SYSTEM INFO's)
        .globl  pumpset_settings
pumpset_settings:
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
        jmp     INVALIDATE

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
        jmp     INVALIDATE
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
        jmp     BLIT

        .balign 4
ps_row: .long   ps_label, ps_select, ps_draw, ps_change
ps_str: .asciz  "PUMP ANIMATION"
        .balign 4
