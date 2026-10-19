/* SPDX-License-Identifier: GPL-2.0-or-later
 * pumpctl: the PUMP master page. Digitone mk1 / Keys, OS 1.43 and 1.44.
 * By Robbie; the master-page plumbing is adapted from Tone+FX (below).
 *
 * One page of our own appended to the stock master pages (FUNC + LFO, then
 * LEFT / RIGHT): the SP-303 Vinyl Sim-style levelling compressor in pump.c.
 *
 *   A = on/off, B = ATK (punch), C = OUT (level, in dB), D = RAT (ratio);
 *   E = CMP, F = REL (LEVEL / GLUE / PUMP), G = WRM, H = MIX.
 *   Press a knob and turn it for big steps; FUNC + turn jumps to an end.
 *
 * The page: a record that turns a bar a turn at the sequencer's tempo (still
 * when it stops), a tonearm that rides in with the gain reduction, the four
 * sliders with their values, and a gain-reduction meter (2 dB segments, a
 * peak tick held 1 s). YES swaps the record for a graph of the last 2.8 s of
 * gain reduction. SETTINGS > PUMP ANIMATION unticked: no animation and no
 * meter (the name and the controls, redrawn twice a second; ~1% of the CPU less).
 *
 * The page plumbing is adapted from Tone+FX's digictl (Digital Alchemy DSP,
 * GPL-2.0-or-later), cut down to this one page: the master view (vtable DN_MASTER_VT)
 * keeps its page kinds in a vector at view+124 (end +128, current index +144);
 * we append one kind and take the view's draw slot. Kind 0 is the value
 * Tone+FX's first page has always used with the stock draw. Settings live in
 * the saved pattern (the third MIDI track's unused 64-byte block, magic
 * "DPM1"), so
 * they follow pattern switches and project saves like stock parameters.
 */
#include "pump_sin.h"
#include "digitone-mk1/core3.h"   /* fw_tempo, fw_transport */

#ifndef DN_TEXTF_ADDR
#define DN_TEXTF_ADDR      0x400DDE68
#endif
#ifndef DN_FONT5_ADDR
#define DN_FONT5_ADDR      0x402315C8
#endif
#ifndef DN_INVALIDATE_ADDR
#define DN_INVALIDATE_ADDR 0x400E512A
#endif
#ifndef DN_OP_NEW_ADDR
#define DN_OP_NEW_ADDR     0x400E944C
#endif
#ifndef DN_STOCK_DRAW_ADDR
#define DN_STOCK_DRAW_ADDR 0x4004DDAE
#endif
#ifndef DN_FW_ADDR
#define DN_FW_ADDR 0x402315DC
#endif
#ifndef DN_FO_ADDR
#define DN_FO_ADDR 0x402316DC
#endif
#ifndef DN_FB_ADDR
#define DN_FB_ADDR 0x402318DC
#endif
#ifndef DN_FBP_ADDR
#define DN_FBP_ADDR 0x40241a44u
#endif
#ifndef DN_PAT_BASE
#define DN_PAT_BASE   0x407FC414u
#endif
#ifndef DN_PAT_PTR
#define DN_PAT_PTR    0x4138E214u
#endif
#ifndef DN_MASTER_VT
#define DN_MASTER_VT  0x40199BAC
#endif

#define DN_TEXTF      ((void (*)(void *, const void *, int, int, int, const char *, ...))DN_TEXTF_ADDR)
#define DN_FONT5      ((const void *)DN_FONT5_ADDR)
#define DN_INVALIDATE ((void (*)(void *))DN_INVALIDATE_ADDR)
/* The firmware returns pointers in %d0 (its own callers test %d0 after
 * malloc); an m68k-linux-gnu build would read %a0. Declared as returning an
 * integer so every toolchain reads %d0, then cast. */
#define DN_OP_NEW(n)  ((int *)((unsigned (*)(unsigned))DN_OP_NEW_ADDR)(n))
#define DN_STOCK_DRAW ((void (*)(void *, void *))DN_STOCK_DRAW_ADDR)
/* The stock drop-down message ("Drive=3"): the firmware's message manager, a
 * singleton (0x4014a060 on 1.44 returns it, creating it the first time), and
 * its printf (0x400ae25c: message manager, format, arguments), the pair the
 * OS uses for "Paste Pattern" or "Sequence cleared!". Both return pointers in
 * %d0, so the getter is declared as returning an integer. OS 1.44 only: its
 * port defines the addresses; without them no message is shown. */
#if defined(DN_POPUP_GET_ADDR) && defined(DN_POPUP_PRINTF_ADDR)
#define DN_POPUP_GET    ((unsigned (*)(void))DN_POPUP_GET_ADDR)
#define DN_POPUP_PRINTF ((void (*)(unsigned, const char *, ...))DN_POPUP_PRINTF_ADDR)
#define PC_POPUP(...)   do { unsigned m_ = DN_POPUP_GET(); \
                             if (m_) DN_POPUP_PRINTF(m_, __VA_ARGS__); } while (0)
#else
#define PC_POPUP(...)   do { } while (0)
#endif
#define DN_FW  ((const unsigned char *)DN_FW_ADDR)
#define DN_FO  ((const unsigned short *)DN_FO_ADDR)
#define DN_FB  ((const unsigned char *)DN_FB_ADDR)
#define DN_FBP (*(volatile unsigned *)DN_FBP_ADDR)

#define PC_KIND       0            /* our page's kind (stock kinds are 15..17) */
#define DN_K_FUNC     1
#define DN_K_YES      13
#define DN_K_PUSH_A   46           /* knob A pressed; .. H = 53 */
#define PC_FAST       8            /* press + turn: steps a detent */
#define DN_K_LEFT     17
#define DN_K_RIGHT    18
#define DC_PER_STEP   4            /* wire counts per encoder detent */
#define DC_ACT_TICKS  20           /* frames a moved slider shows its value */
#define DG_TOP        53           /* rows 53..63 are the stock status bar */

extern int digipump_on, digipump_squash, digipump_pump, digipump_warm, digipump_mix;
extern int digipump_atk, digipump_out, digipump_rat;
extern int digipump_gr;
void digipump_owner_sync(void);

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- per-pattern storage -----------------------------------------------------
 * The patterns are a 0x1611D-byte struct array at DN_PAT_BASE; *DN_PAT_PTR is
 * the current one. Each MIDI track leaves a 64-byte block unused at
 * track_t+0x100; Tone+FX keeps its settings in MIDI tracks 0 and 1's
 * (+0x1040, +0x1410), so we take MIDI track 2's (+0x17E0) and never touch
 * theirs: projects saved under Tone+FX keep its settings. */
#define DN_PAT_STRIDE 0x1611Du
#define DN_PAT_SLOTS  128u
#define PC_STORE_OFF  0x17E0u
#define PC_STORE_N    8
#define PC_STORE_VER  3
#define PC_STORE_LEN  (8 + 2 * PC_STORE_N)
#define PC_STORE_MAG(a) ((a)[0] == 0x44 && (a)[1] == 0x50 && \
                         (a)[2] == 0x4D && (a)[3] == 0x31 && \
                         (a)[5] >= 1 && (a)[5] <= 3)           /* "DPM1" v1..v3 */

static int *const pc_param[PC_STORE_N] = {
    &digipump_on, &digipump_squash, &digipump_pump, &digipump_warm, &digipump_mix,
    &digipump_atk, &digipump_out, &digipump_rat,
};
static const unsigned char pc_def[PC_STORE_N] = { 0, 64, 64, 32, 127, 0, 64, 0 };
static unsigned char *pc_blk;                   /* block we last synced */
static unsigned char pc_copy[PC_STORE_LEN];
static int pc_dirty;                            /* a page edit is pending */
static int pc_sync_ph;

static unsigned char *pc_slot(void)
{
    unsigned p = *(volatile unsigned *)DN_PAT_PTR;
    if (p < DN_PAT_BASE ||
        p >= DN_PAT_BASE + (unsigned)DN_PAT_SLOTS * DN_PAT_STRIDE)
        return 0;
    return (unsigned char *)(p + PC_STORE_OFF);
}

/* a v1 block (PUMP 1.0 .. 1.6) has the first five, v2 (1.7 .. 2.2) seven
 * (its eighth is not RAT); the rest take defaults (RAT: AUTO) */
static void pc_read(const unsigned char *b)
{
    int i, n = b[5] == 1 ? 5 : b[5] == 2 ? 7 : PC_STORE_N;
    for (i = 0; i < PC_STORE_N; i++)
        *pc_param[i] = i < n ? clampi((short)((b[8 + 2 * i] << 8) | b[9 + 2 * i]), 0, 127)
                             : pc_def[i];
}

static void pc_write(unsigned char *b)
{
    int i;
    b[0] = 0x44; b[1] = 0x50; b[2] = 0x4D; b[3] = 0x31;   /* "DPM1" */
    b[4] = 0; b[5] = PC_STORE_VER;
    b[6] = 0; b[7] = PC_STORE_N;
    for (i = 0; i < PC_STORE_N; i++) {
        int v = *pc_param[i];
        b[8 + 2 * i] = (unsigned char)((v >> 8) & 0xff);
        b[9 + 2 * i] = (unsigned char)(v & 0xff);
    }
}

/* Once a UI frame: follow a pattern switch, persist a page edit, and adopt a
 * reload the OS made behind us -- but only a block that still carries our
 * magic; anything else in it is a stock write, and we reclaim the block. */
static void pc_sync(void)
{
    unsigned char *b = pc_slot();
    int i;
    if (!b)
        return;
    if (b == pc_blk) {
        if (pc_dirty) {
            pc_write(b);
        } else if (pc_sync_ph++ & 7) {
            return;                          /* compare every 8th tick */
        } else {
            for (i = 0; i < PC_STORE_LEN; i++)
                if (b[i] != pc_copy[i])
                    break;
            if (i < PC_STORE_LEN) {
                if (PC_STORE_MAG(b))
                    pc_read(b);
                else
                    pc_write(b);
            }
        }
    } else {
        pc_blk = b;                          /* pattern switch / first sync */
        if (PC_STORE_MAG(b))
            pc_read(b);
        else
            pc_write(b);                     /* new pattern: adopt live values */
    }
    pc_dirty = 0;
    for (i = 0; i < PC_STORE_LEN; i++)
        pc_copy[i] = b[i];
}

/* ---- the master view ---------------------------------------------------------*/
static char *pc_view;               /* the master view, from its last draw */
static int pc_vis;                  /* frames left before we disarm */
static int pc_act_id;               /* last-moved slider's encoder (0 = none) */
static int pc_act_ttl;              /* frames left to show that slider's value */
static int pc_func;                 /* FUNC held: knob turns snap to 0 / 127 */
static int pc_ctr;                  /* ticks since the last redraw */
static int pc_rate = 3;             /* ticks between redraws: 3 on PUMP, 8 on a
                                     * stock page (each redraw re-arms pc_vis,
                                     * so the handlers stay live exactly while
                                     * the view is on screen, as in Tone+FX) */
static int pc_eacc[10];             /* per-encoder accumulation (ids 0..9) */
static unsigned char pc_push[8];    /* knob A..H held down: press + turn is fast */

/* Ours AND still open: view+48 is the view's "closed" byte, so a closed view
 * cannot keep our key/encoder handlers armed. */
static int pc_view_open(char *v)
{
    return v && *(int *)v == DN_MASTER_VT && *((unsigned char *)v + 48) == 0;
}

static int pc_live(void)
{
    return pc_vis > 0 && pc_view_open(pc_view);
}

static int pc_nkinds(char *view, int **vp)
{
    int *v = *(int **)(view + 124), *e = *(int **)(view + 128);
    *vp = v;
    if (!v || !e || e <= v)
        return 0;
    return (int)(e - v);
}

static int pc_cur_kind(char *view)
{
    int *v, n = pc_nkinds(view, &v), i = *(int *)(view + 144);
    if (i < 0 || i >= n)
        return -1;
    return v[i];
}

/* Append our kind once, after the stock pages. */
static void pc_add_page(char *view)
{
    int *v, *nv, n = pc_nkinds(view, &v), i;
    if (n < 1 || n > 14)
        return;
    for (i = 0; i < n; i++)
        if (v[i] == PC_KIND)
            return;
    nv = DN_OP_NEW(4u * (unsigned)(n + 1));
    if (!nv)
        return;
    for (i = 0; i < n; i++)
        nv[i] = v[i];
    nv[n] = PC_KIND;
    *(int **)(view + 124) = nv;
    *(int **)(view + 128) = nv + n + 1;
    *(int **)(view + 132) = nv + n + 1;
}

/* ---- drawing (direct framebuffer, 1 bpp column-major; y = 0 at the bottom,
 * the stock convention) -------------------------------------------------------*/
static unsigned char *dg_fb;

static void dg_fill(int x0, int y0, int x1, int y1, int c)
{
    int x, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 < y1) { t = y0; y0 = y1; y1 = t; }        /* y0 = the top row */
    if (x0 < 0) x0 = 0;
    if (x1 > 127) x1 = 127;
    if (y1 < 0) y1 = 0;
    if (y0 > 63) y0 = 63;
    for (x = x0; x <= x1; x++) {
        int rt = 63 - y0, rb = 63 - y1, p;
        unsigned char *col = dg_fb + (x << 3);
        for (p = rt >> 3; p <= (rb >> 3); p++) {
            unsigned char m = 0xffu;
            if (p == (rt >> 3)) m &= (unsigned char)(0xffu << (rt & 7));
            if (p == (rb >> 3)) m &= (unsigned char)(0xffu >> (7 - (rb & 7)));
            if (c) col[7 - p] |= m; else col[7 - p] &= (unsigned char)~m;
        }
    }
}

static void dg_frame(int x0, int y0, int x1, int y1)
{
    dg_fill(x0, y0, x1, y0, 1);
    dg_fill(x0, y1, x1, y1, 1);
    dg_fill(x0, y0, x0, y1, 1);
    dg_fill(x1, y0, x1, y1, 1);
}

static int dg_strw(const char *s, int sc)
{
    int w = 0;
    for (; *s; s++)
        w += (DN_FW[(unsigned char)*s] + 1) * sc;
    return w;
}

static void dg_glyph(int x, int ytop, int ch, int sc)
{
    int w = DN_FW[ch & 0xff];
    int off = DN_FO[ch & 0xff];
    int c, r;
    for (c = 0; c < w; c++) {
        int bits = DN_FB[off + c];
        for (r = 0; r < 8; r++) {
            if (bits & (1 << r)) {              /* bit 0 = top of the glyph */
                int px = x + c * sc;
                int py = ytop - r * sc;
                if (sc == 1) {                  /* one pixel: set it, no fill */
                    if ((unsigned)px < 128u && (unsigned)py < 64u) {
                        int rr = 63 - py;
                        dg_fb[(7 - (rr >> 3)) + (px << 3)] |= (unsigned char)(1u << (rr & 7));
                    }
                } else {
                    dg_fill(px, py, px + sc - 1, py - sc + 1, 1);
                }
            }
        }
    }
}

static void dg_text(int x, int ytop, const char *s, int sc)
{
    for (; *s; s++) {
        dg_glyph(x, ytop, (unsigned char)*s, sc);
        x += (DN_FW[(unsigned char)*s] + 1) * sc;
    }
}

static void dg_offbox(void)
{
    const char *s = "OFF";
    int w = dg_strw(s, 1);
    int x0 = (128 - (w + 12)) / 2, x1 = x0 + w + 11;
    dg_fill(x0, 51, x1, 37, 0);
    dg_frame(x0, 51, x1, 37);
    dg_text((128 - w) / 2, 48, s, 1);
}

static const char *dg_pumpname(void)
{
    static const char *const n[3] = { "LEVEL", "GLUE", "PUMP" };
    int b = (digipump_pump * 3) >> 7;
    return n[b > 2 ? 2 : b];
}

/* ---- the page, in screen coordinates (m_*: x right, y down from the top of
 * the panel, as the preview was drawn; rows 0..10 are the stock title bar) ---- */
static void m_fill(int x0, int y0, int x1, int y1, int c) { dg_fill(x0, 63 - y0, x1, 63 - y1, c); }
static void m_px(int x, int y, int c)
{
    int r;
    if ((unsigned)x > 127u || (unsigned)y > 63u)
        return;
    r = y;                                         /* screen row */
    if (c)
        dg_fb[(7 - (r >> 3)) + (x << 3)] |= (unsigned char)(1u << (r & 7));
    else
        dg_fb[(7 - (r >> 3)) + (x << 3)] &= (unsigned char)~(1u << (r & 7));
}
static void m_frame(int x0, int y0, int x1, int y1)
{
    m_fill(x0, y0, x1, y0, 1); m_fill(x0, y1, x1, y1, 1);
    m_fill(x0, y0, x0, y1, 1); m_fill(x1, y0, x1, y1, 1);
}
static int m_w(const char *s) { return dg_strw(s, 1) - 1; }
static void m_text(int x, int y, const char *s) { dg_text(x, 63 - y, s, 1); }
static void m_ctext(int cx, int y, const char *s) { m_text(cx - m_w(s) / 2, y, s); }
static void m_rtext(int xr, int y, const char *s) { m_text(xr - m_w(s), y, s); }
static void m_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy, e2, n = 256;           /* n: a hard stop, whatever happens */
    while (n-- > 0) {
        m_px(x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * e;                         /* both tests on the same error */
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
/* r * cos / sin of a (0..255 a turn), rounded */
static int m_cos(int r, int a) { return (r * sin256[(a + 64) & 255] + 16384) >> 15; }
static int m_sin(int r, int a) { return (r * sin256[a & 255] + 16384) >> 15; }
static void m_ring(int cx, int cy, int r, int n, int gap_at, int gap)
{
    int i;
    for (i = 0; i < n; i++) {
        int a = (i * 256) / n;
        if (gap && ((a - gap_at) & 255) < gap)
            continue;                              /* the sheen: where light catches */
        m_px(cx + m_cos(r, a), cy + m_sin(r, a), 1);
    }
}
static void m_itoa(char *b, int v)
{
    int n = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    if (v >= 100) b[n++] = (char)('0' + v / 100);
    if (v >= 10) b[n++] = (char)('0' + (v / 10) % 10);
    b[n++] = (char)('0' + v % 10);
    b[n] = 0;
}

/* The animation's state, moved on by pumpctl_tick */
#define PC_HIST 56                          /* graph: 2 px a redraw, 2.8 s */
static unsigned pc_ang;                     /* the record's turn, Q8 of 256 a turn */
static unsigned char pc_hist[PC_HIST];      /* gain reduction per redraw, dB */
static int pc_hpos;
static int pc_gr_db, pc_pk_db, pc_pk_hold;  /* the meter, its peak and hold */
static int pc_graph;                        /* YES: the record or the graph */

/* SETTINGS > PUMP ANIMATION (pumpset.s): byte 0 set = still. Saved with the
 * project (core's project data, "PMPU"); on OS 1.44 a change also asks the
 * OS to refresh its power-up copy of the project, as digisnake does, so it
 * survives a power cycle. */
unsigned char pumpctl_anim_off[4];
static void pc_prefs_loaded(int32_t found) { (void)found; }   /* none: zeros, animate */
const struct core_projdata pumpctl_projdata = {
    0x504d5055u,                            /* "PMPU" */
    sizeof pumpctl_anim_off, pumpctl_anim_off, pc_prefs_loaded
};
#define pc_still() (pumpctl_anim_off[0] != 0)

#define PC_SAVE_TICKS 90                    /* 1.5 s of quiet, then the request */
static unsigned short pc_save_wait;
static unsigned char pc_anim_last;
static void pc_mirror_request(void)
{
#ifdef PC_MIRROR
    uint8_t *p = fw_project;
    uint32_t buf;
    if (!p)
        return;
    buf = *(volatile uint32_t *)(p + 32);
    if (!buf || *(volatile uint8_t *)(p + 36))
        return;
    ((void (*)(void *, uint32_t, uint32_t))(*(uint32_t **)p)[17])(p, buf, 0);
#endif
}

static int pc_db(int lu) { return lu > 0 ? (lu * 385) >> 14 : 0; }   /* lu -> dB */

/* Each redraw on PUMP: the gain reduction since the last one (its peak, from
 * the render), into the meter, its peak hold and the graph. */
static void pc_meter_step(void)
{
    extern int digipump_grpk;
    int d = digipump_on ? pc_db(digipump_grpk) : 0;
    digipump_grpk = 0;
    pc_gr_db = d;
    if (d >= pc_pk_db) {
        pc_pk_db = d;
        pc_pk_hold = 20;                        /* 1 s */
    } else if (pc_pk_hold > 0) {
        pc_pk_hold--;
    } else if (pc_pk_db > 0) {
        pc_pk_db--;
    }
    pc_hist[pc_hpos] = (unsigned char)(d > 255 ? 255 : d);
    pc_hpos = (pc_hpos + 1) % PC_HIST;
}

/* B's meter: 12 segments of 2 dB from the top, a peak tick, the dB below */
static void m_meter(void)
{
    char b[6];
    int i, pi;
    m_ctext(124, 12, "GR");
    for (i = 0; i < 12; i++) {
        int y = 21 + i * 3;
        if (pc_gr_db > i * 2 + 1)
            m_fill(121, y, 126, y + 1, 1);
        else {
            m_px(121, y, 1);
            m_px(126, y, 1);
        }
    }
    pi = pc_pk_db / 2;
    if (pi > 11) pi = 11;
    if (pc_pk_db > 1)
        m_fill(120, 22 + pi * 3, 127, 22 + pi * 3, 1);
    m_itoa(b, pc_gr_db);
    m_ctext(124, 56, b);
}

/* D: the record turns with the tempo; the tonearm rides in with the gain
 * reduction (0..24 dB: 0..28 degrees) */
static void m_record(void)
{
    int cx = 21, cy = 33, x, y, r, a = (int)(pc_ang >> 8) & 255;
    int ax = 41, ay = 13, L = 25, ta, nx, ny, gr = pc_gr_db > 24 ? 24 : pc_gr_db;
    m_ring(cx, cy, 18, 120, 0, 0);
    m_ring(cx, cy, 14, 98, a, 29);
    m_ring(cx, cy, 10, 70, a + 128, 20);
    for (y = -5; y <= 5; y++)                       /* the label */
        for (x = -5; x <= 5; x++)
            if (x * x + y * y <= 25)
                m_px(cx + x, cy + y, 1);
    m_px(cx, cy, 0);
    for (r = 2; r <= 4; r++)                        /* its stripe, turning */
        m_px(cx + m_cos(r, a), cy + m_sin(r, a), 0);
    ta = 77 + (gr * 20) / 24;                       /* 108 .. 136 degrees, of 256 */
    nx = ax + m_cos(L, ta);
    ny = ay + m_sin(L, ta);
    m_fill(ax - 2, ay - 2, ax + 2, ay + 2, 1);
    m_px(ax, ay, 0);
    m_line(ax, ay, nx, ny);
    m_fill(nx - 1, ny - 1, nx + 1, ny + 1, 1);
    m_ctext(21, 55, "PUMP");
}

/* RAT as shown: "AUT", the ratio to :1 (whole), "LIM"; the popup in tenths */
static int pc_rat_tenths(int v)                 /* 20 .. 200 */
{
    int lu = 256 + ((v - 1) * 850) / 125, r = 0;
    /* 2^(lu/256) * 10, from the 6 dB octaves and a linear fraction: close
     * enough for a label (the DSP uses the exact table) */
    int n = lu >> 8, f = lu & 255;
    r = (10 << n) + (((10 << n) * f) >> 8);
    return r;
}

/* OUT in tenths of a dB: 64 = 0, 0.5 dB a step below, 6/63 dB above */
static int pc_out_tenths(int v)
{
    return v <= 64 ? (v - 64) * 5 : ((v - 64) * 60) / 63;
}

/* The controls as the knobs sit: A..D on top, E..H below. Each cell: its
 * name, its value and a bar; OUT's bar grows from the middle (0 dB). */
static void m_cell(int x, int y, const char *lab, int v, int bip)
{
    char b[6];
    int cx = x + 8;
    m_ctext(cx, y, lab);
    if (bip == 2) {                                       /* RAT */
        if (v == 0) { b[0] = 'A'; b[1] = 'U'; b[2] = 'T'; b[3] = 0; }
        else if (v == 127) { b[0] = 'L'; b[1] = 'I'; b[2] = 'M'; b[3] = 0; }
        else m_itoa(b, (pc_rat_tenths(v) + 5) / 10);
    } else if (bip) {
        int t = pc_out_tenths(v);                         /* whole dB, rounded */
        m_itoa(b, (t + (t < 0 ? -5 : 5)) / 10);
    } else {
        m_itoa(b, v);
    }
    m_ctext(cx, y + 8, b);
    m_fill(x, y + 18, x + 16, y + 18, 1);                 /* the track */
    if (bip == 1) {
        int c = x + 8, e = x + 8 + ((v - 64) * 8) / 64;
        m_fill(c, y + 16, e, y + 18, 1);
        m_fill(c, y + 16, c, y + 19, 1);                  /* the centre */
    } else {
        m_fill(x, y + 16, x + (v * 16) / 127, y + 18, 1);
    }
}

static void m_sliders(void)
{
    int x0 = 45, dx = 19;
    m_ctext(x0 + 8, 13, "PMP");
    m_ctext(x0 + 8, 21, digipump_on ? "ON" : "OFF");
    m_cell(x0 + dx, 13, "ATK", digipump_atk, 0);
    m_cell(x0 + 2 * dx, 13, "OUT", digipump_out, 1);
    m_cell(x0 + 3 * dx, 13, "RAT", digipump_rat, 2);
    m_cell(x0, 37, "CMP", digipump_squash, 0);
    m_cell(x0 + dx, 37, "REL", digipump_pump, 0);
    m_cell(x0 + 2 * dx, 37, "WRM", digipump_warm, 0);
    m_cell(x0 + 3 * dx, 37, "MIX", digipump_mix, 0);
}

/* C: the last 2.8 s of gain reduction, hanging from the top line; dotted
 * lines at 6, 12 and 18 dB */
static void m_graph(void)
{
    char b[8];
    int i, k, x;
    m_fill(1, 12, 112, 12, 1);
    for (k = 6; k <= 18; k += 6)
        for (x = 1; x < 112; x += 4)
            m_px(x, 12 + (k * 34) / 24, 1);
    for (i = 0; i < PC_HIST; i++) {
        int d = pc_hist[(pc_hpos + i) % PC_HIST];
        if (d > 24) d = 24;
        m_fill(1 + 2 * i, 12, 2 + 2 * i, 12 + (d * 34) / 24, 1);
    }
    x = 1;
    m_text(x, 55, "CMP"); x += m_w("CMP") + 3;
    m_itoa(b, digipump_squash); m_text(x, 55, b); x += m_w(b) + 6;
    m_text(x, 55, "REL"); x += m_w("REL") + 3;
    m_itoa(b, digipump_pump); m_text(x, 55, b); x += m_w(b) + 6;
    m_text(x, 55, "WRM"); x += m_w("WRM") + 3;
    m_itoa(b, digipump_warm); m_text(x, 55, b);
}

static void pc_page(void *bmp)
{
    (void)bmp;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    if (pc_still()) {                           /* STILL: no animation */
        dg_text(4, 63 - 30, "PUMP", 2);
        m_sliders();
    } else if (pc_graph) {
        m_graph();
    } else {
        m_record();
        m_sliders();
    }
    if (!pc_still())                            /* no meter when still: nothing moves */
        m_meter();
    if (!digipump_on)
        dg_offbox();
}

/* vtable slot 4 of the master view: draw */
void pumpctl_mdraw(void *view, void *bmp)
{
    pc_add_page((char *)view);
    DN_STOCK_DRAW(view, bmp);
    pc_view = (char *)view;
    if (pc_cur_kind(pc_view) != PC_KIND)
        pc_rate = 8;
    else
        pc_rate = pc_still() ? 30 : 3;          /* STILL: 2 fps, the keepalive; else 20 */
    pc_vis = pc_cur_kind(pc_view) == PC_KIND && !pc_still() ? 3 * pc_rate + 2 : pc_rate + 2;
    pc_ctr = 0;
    if (pc_cur_kind(pc_view) != PC_KIND)
        return;
    dg_fb = (unsigned char *)DN_FBP;
    if (dg_fb)
        pc_page(bmp);
}

/* Every UI frame: storage, and a redraw every pc_rate frames while the master
 * view is on screen (20 fps on PUMP, so the record, the arm and the meter move smoothly).
 * Leaving the view stops the draws, pc_vis runs out and we disarm. */
void pumpctl_tick(void *ctrl)
{
    (void)ctrl;
    pc_sync();
    if (pumpctl_anim_off[0] != pc_anim_last) {     /* SETTINGS changed it */
        pc_anim_last = pumpctl_anim_off[0];
        pc_save_wait = PC_SAVE_TICKS;
    }
    if (pc_save_wait && !--pc_save_wait)
        pc_mirror_request();
    digipump_owner_sync();               /* hold the insert while on or fading */
    if (pc_act_ttl > 0) {
        pc_act_ttl--;
        if (!pc_act_ttl && pc_view_open(pc_view))
            DN_INVALIDATE(pc_view);
    }
    if (pc_vis)
        pc_vis--;
    /* the record: a turn a bar (4 beats) while the sequencer plays. A tick is
     * 1/60 s; fw_tempo is BPM x 120, so a tick turns BPM / 14400 of a turn:
     * fw_tempo / 1728000, here in Q16 of a turn (65536 = a turn). */
    if (fw_transport == 1 && fw_tempo > 0)
        pc_ang += ((unsigned)fw_tempo * 2485u) >> 16;
    if (pc_vis > 0 && pc_view_open(pc_view)) {
        if (++pc_ctr >= pc_rate) {
            pc_ctr = 0;
            if (pc_cur_kind(pc_view) == PC_KIND) {
                pc_meter_step();
            }
            DN_INVALIDATE(pc_view);
        }
    } else if (pc_view && !pc_view_open(pc_view)) {
        pc_vis = 0;
    }
}

/* stock convention: one 0..127 step per detent (4 wire counts) */
static int pc_steps(int id, int d)
{
    int st;
    if ((d > 0 && pc_eacc[id] < 0) || (d < 0 && pc_eacc[id] > 0))
        pc_eacc[id] = 0;
    pc_eacc[id] += d;
    st = pc_eacc[id] / DC_PER_STEP;
    pc_eacc[id] -= st * DC_PER_STEP;
    return st;
}

int pumpctl_enc(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12);
    int delta = *(int *)((char *)ev + 16);
    int d;
    (void)brain;
    if (id < 1 || id > 8 || !pc_live() || pc_cur_kind(pc_view) != PC_KIND)
        return 0;                        /* not ours (LEVEL/DATA included, so FUNC +
                                          * LEVEL still opens the sound browser): the
                                          * stock handles it */
    if (!delta)
        return 1;
    d = pc_steps(id, delta);
    if (!d)
        return 1;
    if (pc_func)
        d = d > 0 ? 127 : -127;          /* FUNC + turn: snap to 100% / 0% */
    else if (pc_push[id - 1])
        d *= PC_FAST;                    /* press + turn: big steps, as on the
                                          * stock pages */
    switch (id) {               /* the edit, and the stock Name=value message */
    case 1:
        digipump_on = (d > 0);
        PC_POPUP("Pump=%s", digipump_on ? "ON" : "OFF");
        break;
    case 2:
        digipump_atk = clampi(digipump_atk + d, 0, 127);
        PC_POPUP("Attack=%d", digipump_atk);
        break;
    case 4:
        digipump_rat = clampi(digipump_rat + d, 0, 127);
        if (digipump_rat == 0)
            PC_POPUP("Ratio=AUTO");
        else if (digipump_rat == 127)
            PC_POPUP("Ratio=LIMIT");
        else {
            int r = pc_rat_tenths(digipump_rat);
            PC_POPUP("Ratio=%d.%d:1", r / 10, r % 10);
        }
        break;
    case 3:
        digipump_out = clampi(digipump_out + d, 0, 127);
        {
            int t = pc_out_tenths(digipump_out), a = t < 0 ? -t : t;
            PC_POPUP("Out=%c%d.%ddB", t < 0 ? '-' : '+', a / 10, a % 10);
        }
        break;
    case 5:
        digipump_squash = clampi(digipump_squash + d, 0, 127);
        PC_POPUP("Comp=%d", digipump_squash);
        break;
    case 6:
        digipump_pump = clampi(digipump_pump + d, 0, 127);
        PC_POPUP("Release=%s %d", dg_pumpname(), digipump_pump);
        break;
    case 7:
        digipump_warm = clampi(digipump_warm + d, 0, 127);
        PC_POPUP("Warmth=%d", digipump_warm);
        break;
    case 8:
        digipump_mix = clampi(digipump_mix + d, 0, 127);
        PC_POPUP("Mix=%d", digipump_mix);
        break;
    default:
        return 1;
    }
    if (id >= 2) {
        pc_act_id = id;
        pc_act_ttl = DC_ACT_TICKS;
    }
    pc_dirty = 1;
    DN_INVALIDATE(pc_view);
    return 1;
}

/* FUNC is tracked (never consumed). LEFT / RIGHT rotate the master page list,
 * stock pages and ours, while the master view is open. Nothing else is taken. */
int pumpctl_key(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12);
    int flags = *(int *)((char *)ev + 16);
    int *v, n, i;
    (void)brain;
    if (id >= DN_K_PUSH_A && id < DN_K_PUSH_A + 8) {   /* followed always, never taken */
        pc_push[id - DN_K_PUSH_A] = (unsigned char)((flags & 1) != 0);
        return 0;
    }
    if (!pc_live())
        return 0;
    if (id == DN_K_FUNC) {
        pc_func = (flags & 1) != 0;
        return 0;
    }
    if (id == DN_K_YES && pc_cur_kind(pc_view) == PC_KIND) {   /* RECORD, GRAPH */
        if ((flags & 1) && !(flags & 0x10) && !(flags & 8)) {
            if (pc_still()) {
                PC_POPUP("Animation off: SETTINGS");
            } else {
                pc_graph = !pc_graph;
                PC_POPUP("View=%s", pc_graph ? "GRAPH" : "RECORD");
                pc_ctr = 0;
                DN_INVALIDATE(pc_view);
            }
        }
        return 1;                        /* YES's other events on PUMP: ours too */
    }
    if (id != DN_K_LEFT && id != DN_K_RIGHT)
        return 0;
    if (!(flags & 1) || (flags & 0x10) || (flags & 8))
        return 0;                        /* presses only, no repeats */
    n = pc_nkinds(pc_view, &v);
    if (n < 1)
        return 0;
    i = *(int *)(pc_view + 144);
    if (i < 0 || i >= n)
        i = 0;
    *(int *)(pc_view + 144) = (i + (id == DN_K_RIGHT ? 1 : n - 1)) % n;
    DN_INVALIDATE(pc_view);
    return 1;
}
