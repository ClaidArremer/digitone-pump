/* SPDX-License-Identifier: GPL-2.0-or-later
 * pumpdt: the PUMP master page on the Digitakt mk1, OS 1.53 and 1.54.
 * By Robbie. The page and its drawing are the Digitone's (pumpctl.c); what
 * holds it up is core 4.1's master pages (core_mpages) and master output
 * (ev_render_master), so it names no firmware address for those.
 *
 * PUMP takes the stock compressor's place: its page among the master pages
 * (FUNC + LFO), its eight parameters for its settings, and its slot in the
 * render (pump.c, digipump_comp). The SP-303 Vinyl Sim-style levelling
 * compressor in pump.c runs at the master output instead.
 *
 *   A = on/off, B = ATK (punch), C = OUT (level, in dB), D = RAT (ratio:
 *   AUTO, 2:1, 3:1, 4:1, 6:1, 10:1, 20:1, LIMIT); E = CMP, F = REL (LEVEL /
 *   GLUE / PUMP), G = WRM, H = MIX.
 *   Press a knob and turn it to move in 8s; FUNC + turn jumps to an end.
 *   YES: the record or the gain-reduction graph. CLEAR (FUNC + held LFO +
 *   PLAY): back to the defaults, PUMP off.
 *
 * The page: a record that turns a bar a turn at the sequencer's tempo (still
 * when it stops), a tonearm that rides in with the gain reduction, the eight
 * controls with their values, and a gain-reduction meter (2 dB segments, a
 * peak tick held 1 s), at 20 frames a second, paced by the audio blocks.
 * SETTINGS > GLOBAL FX/MIX > PUMP ANIMATION unticked: no animation and no
 * meter.
 *
 * The settings are the compressor's: kept with each pattern's kit (or
 * global, when GLOBAL FX/MIX makes the compressor global), saved with the
 * project and over a power-off, as the stock's were.
 */
#include <stdint.h>
#include "../../src/pump_sin.h"   /* the shared sources: ../../src */
#include "digitakt-mk1/core3.h"

/* firmware locations (mod.json's defsym, one set per OS) */
#define DT_TRANSPORT (*(volatile int32_t *)DT_TRANSPORT_ADDR)   /* 0 stopped, 1 playing, 2 paused */
#define DT_TEMPO     (*(volatile int32_t *)DT_TEMPO_ADDR)       /* BPM x 120 */
#define DN_FW  ((const unsigned char *)DT_FONT_W_ADDR)         /* the stock 5-px font: widths, */
#define DN_FO  ((const unsigned short *)DT_FONT_O_ADDR)        /* offsets, */
#define DN_FB  ((const unsigned char *)DT_FONT_B_ADDR)         /* columns */

#define DT_K_FUNC     1
#define DT_K_YES      12
#define DT_K_PUSH_A   40           /* knob A pressed; .. H = 47 */
#define PC_FAST       8            /* pressed + turn: to the next multiple of 8 */
#define DT_PER_STEP   16           /* knob counts a detent on the Digitakt */
#define DG_TOP        53           /* rows 53..63 are the stock title bar */
#define PC_FRAME      75           /* audio blocks a frame: 1500 / 75 = 20 fps */
#define PC_FRAME_STILL 750         /* still: 2 fps */

extern int digipump_on, digipump_squash, digipump_pump, digipump_warm, digipump_mix;
extern int digipump_atk, digipump_out, digipump_rat;
extern int digipump_gr;
extern volatile unsigned digipump_blocks;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- the settings: the stock compressor's eight parameters ------------------
 * The compressor is gone (pump.c, digipump_comp); its parameters, ids 86..93,
 * hold PUMP's: the firmware keeps them per pattern in the kit (or globally,
 * with SETTINGS > GLOBAL FX/MIX's compressor set so), saves them with the
 * project and over a power-off, and the render hands them to digipump_comp,
 * which sets digipump_* from them. An edit goes through the compressor page's
 * own edit (the master view's vtable slot 22, 0x400388f6: id, a change in 8.8,
 * kit or global as the stock decides, clamped to the stock range), with the
 * page shown taken for the compressor's (kind 11) for that one call. */
#define CP_THR 86                   /* CMP */
#define CP_ATK 87                   /* ATK */
#define CP_REL 88                   /* REL */
#define CP_MUP 89                   /* WRM */
#define CP_RAT 90                   /* RAT, 0..7 */
#define CP_SCS 91                   /* on: 0 off, else on (0..10) */
#define CP_SCF 92                   /* OUT */
#define CP_MIX 93                   /* MIX */
#define DT_COMP_EDIT ((int32_t (*)(void *, int32_t, int32_t, int32_t, void *))0x400388f6)
extern unsigned char digipump_stored[8];
extern const unsigned char digipump_rat_tab[8];
/* the stock compressor's defaults (THR .. MIX; a kit PUMP has not touched) and
 * PUMP's, as stored */
static const unsigned char cp_stock[8] = { 32, 24, 32, 64, 3, 0, 80, 0 };
static const unsigned char cp_pump[8]  = { 64, 0, 64, 32, 0, 0, 64, 127 };

static int pc_kinds(char *view, int32_t **vp)
{
    int32_t *v = *(int32_t **)(view + 124), *e = *(int32_t **)(view + 128);
    *vp = v;
    if (!v || !e || e <= v || e - v > 32)
        return 0;
    return (int)(e - v);
}

/* parameter id (86..93) by a change of n steps (8.8: n << 8), while our page
 * is shown */
static void pc_edit(void *view, int id, int n)
{
    int32_t *v;
    int k = pc_kinds((char *)view, &v), i = *(int32_t *)((char *)view + 144);
    if (!n || i < 0 || i >= k || v[i] != 0)
        return;
    v[i] = 11;                                   /* the compressor's page, for the edit */
    DT_COMP_EDIT(view, id, n * 256, 0, 0);
    v[i] = 0;
}

/* stored value of id to x (0..127) */
static void pc_set(void *view, int id, int x)
{
    pc_edit(view, id, x - (int)digipump_stored[id - CP_THR]);
    digipump_stored[id - CP_THR] = (unsigned char)x;
}

/* PUMP's defaults into all eight (on: as given) */
static void pc_defaults(void *view, int on)
{
    int k;
    for (k = 0; k < 8; k++)
        if (k != CP_SCS - CP_THR)
            pc_set(view, CP_THR + k, cp_pump[k]);
    pc_set(view, CP_SCS, on);
}

/* The stock compressor's page out of the master pages: ours follows it
 * (after = 11), so it takes its place. Done again whenever core or the
 * firmware builds the list anew. Core's places for the pages after it move
 * down one. */
extern struct core_mpage *core_mpages[];
static void pc_drop_comp(void *view)
{
    int32_t *v;
    int n = pc_kinds((char *)view, &v), i, j, cur;
    struct core_mpage **mp;
    for (i = 0; i < n; i++)
        if (v[i] == 11)
            break;
    if (i >= n || i + 1 >= n || v[i + 1] != 0)
        return;                                  /* none, or core has not placed ours */
    for (j = i; j < n - 1; j++)
        v[j] = v[j + 1];
    *(int32_t **)((char *)view + 128) = v + n - 1;
    cur = *(int32_t *)((char *)view + 144);
    if (cur > i)
        *(int32_t *)((char *)view + 144) = cur - 1;
    for (mp = core_mpages; *mp; mp++)
        if ((*mp)->view == view && (*mp)->pos > i)
            (*mp)->pos--;
    fw_invalidate(view);
}

/* ---- the page ------------------------------------------------------------------ */
struct core_mpage pumpdt_page;
static void *pc_view;               /* the master view, from its last draw */
static int pc_func;                 /* FUNC held: knob turns snap to 0 / 127 */
static int pc_eacc[10];             /* per-knob counts since the last step */
static unsigned pc_blk_last;        /* digipump_blocks at the last tick */
static int pc_due;                  /* blocks toward the next frame */
static unsigned pc_draws_seen;      /* core_mview_draws at the last tick */
static int pc_quiet;                /* ticks with no draw of the master view */

/* SETTINGS > GLOBAL FX/MIX > PUMP ANIMATION (pumpdtset.s): byte 0 set = still */
unsigned char pumpctl_anim_off[4];
#define pc_still() (pumpctl_anim_off[0] != 0)

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

/* ---- core 4.1's master page ------------------------------------------------- */

/* draw(view, bmp), after the stock draw (the title bar). The screen's bitmap:
 * +4 width, +8 height, +12 longs a column, +16 the pixels: 128 columns of two
 * longs, bit 31 of a column's first long its row 0 (the bottom), the same
 * bytes the Digitone's frame buffer has, so the Digitone's drawing runs as
 * it is. Any other shape: the page is left as the stock drew it. */
static void pumpdt_draw(void *view, void *bmp)
{
    char *b = (char *)bmp;
    pc_view = view;
    if (*(int32_t *)(b + 4) != 128 || *(int32_t *)(b + 8) != 64 ||
        *(int32_t *)(b + 12) != 2 || !*(unsigned char **)(b + 16)) {
        /* not the shape we know: the values in the stock's text, still usable */
        fw_fillrect(bmp, 0, 0, 127, DG_TOP - 1, 0);
        fw_textf(bmp, fw_font5, 2, 40, -1, "PUMP %s  ATK %d OUT %d RAT %d",
                 digipump_on ? "ON" : "OFF", digipump_atk, digipump_out, digipump_rat);
        fw_textf(bmp, fw_font5, 2, 24, -1, "CMP %d REL %d WRM %d MIX %d",
                 digipump_squash, digipump_pump, digipump_warm, digipump_mix);
        return;
    }
    dg_fb = *(unsigned char **)(b + 16);
    pc_page(bmp);
}

static int pc_shown(void)
{
    void *v = core_mview;
    return v && core_mpage_shown(v) == &pumpdt_page;
}

/* ev_tick: the pattern's values, the record's turn, and a frame every 75
 * audio blocks (20 a second; still, 2) while the page is on the screen. */
void pumpdt_tick(void *ctrl)
{
    unsigned blk = digipump_blocks, d = blk - pc_blk_last;
    pc_blk_last = blk;
    if (d > 3000)
        d = 3000;
    if (core_mview)
        pc_drop_comp(core_mview);
    /* the record: a turn a bar (4 beats) while the sequencer plays. A block
     * is 1/1500 s and DT_TEMPO is BPM x 120, so a block turns DT_TEMPO /
     * 43200000 of a turn: (DT_TEMPO x 397) >> 10 in Q24 (0.2% slow) */
    if (DT_TRANSPORT == 1 && DT_TEMPO > 0 && DT_TEMPO < 400 * 120) {
        static unsigned ang24;
        ang24 += d * (((unsigned)DT_TEMPO * 397u) >> 10);
        pc_ang = ang24 >> 8;                    /* Q16 of a turn, as m_record reads it */
    }
    if (core_mview_draws != pc_draws_seen) {
        pc_draws_seen = core_mview_draws;
        pc_quiet = 0;
    } else if (pc_quiet < 1000) {
        pc_quiet++;
    }
    if (!pc_shown() || pc_quiet > 60) {        /* not up, or the view is closed */
        pc_due = 0;
        return;
    }
    pc_due += (int)d;
    if (pc_due < (pc_still() ? PC_FRAME_STILL : PC_FRAME))
        return;
    pc_due -= pc_still() ? PC_FRAME_STILL : PC_FRAME;
    if (pc_due > PC_FRAME)
        pc_due = 0;                             /* fell behind: no catching up */
    pc_meter_step();
    *((unsigned char *)ctrl + 0x20) = 1;        /* draw the screen again */
}

/* a value moved d detents: one a detent, or with the knob pressed, from
 * multiple of 8 to multiple of 8 (61 -> 64 -> 72; 0 .. 127) */
static int pc_fast;
static unsigned char pc_push[8];            /* knob A..H pressed (keys 40..47) */
static int pc_move(int v, int d)
{
    if (pc_fast)
        v = d > 0 ? ((v / PC_FAST) + d) * PC_FAST : (((v + PC_FAST - 1) / PC_FAST) + d) * PC_FAST;
    else
        v += d;
    return clampi(v, 0, 127);
}

/* one 0..127 step a detent (16 counts on the Digitakt) */
static int pc_steps(int id, int d)
{
    int st;
    if ((d > 0 && pc_eacc[id] < 0) || (d < 0 && pc_eacc[id] > 0))
        pc_eacc[id] = 0;
    pc_eacc[id] += d;
    st = pc_eacc[id] / DT_PER_STEP;
    pc_eacc[id] -= st * DT_PER_STEP;
    return st;
}

/* enc(view, event): +12 knob 1-8 = A-H (9 LEVEL), +16 the turn, +20 set while
 * the knob is held down */
static int32_t pumpdt_enc(void *view, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12);
    int delta = *(int32_t *)((char *)ev + 16);
    int d;
    if (id < 1 || id > 8)
        return 0;                               /* LEVEL: the stock's */
    d = pc_steps(id, delta);
    if (!d)
        return 1;
    if (pc_func)
        d = d > 0 ? 127 : -127;                 /* FUNC + turn: to an end */
    pc_fast = !pc_func && pc_push[id - 1];      /* pressed + turn: snaps to 8s */
    switch (id) {
    case 1:                                     /* on / off */
        if (d > 0 && !digipump_on) {
            int k, fresh = 1;                   /* a kit PUMP has never set: its defaults */
            for (k = 0; k < 8; k++)
                if (k != CP_SCS - CP_THR && digipump_stored[k] != cp_stock[k])
                    fresh = 0;
            if (fresh)
                pc_defaults(view, 1);
            else
                pc_set(view, CP_SCS, 1);
            digipump_on = 1;
        } else if (d < 0 && digipump_on) {
            pc_set(view, CP_SCS, 0);
            digipump_on = 0;
        }
        break;
    case 4:                                     /* RAT: 8 steps */
        if (d > 7) d = 7;
        if (d < -7) d = -7;
        pc_set(view, CP_RAT, clampi(digipump_stored[CP_RAT - CP_THR] + d, 0, 7));
        digipump_rat = digipump_rat_tab[digipump_stored[CP_RAT - CP_THR] & 7];
        break;
    default: {
        static const unsigned char ids[9] = { 0, 0, CP_ATK, CP_SCF, 0, CP_THR, CP_REL, CP_MUP, CP_MIX };
        static int *const vals[9] = { 0, 0, &digipump_atk, &digipump_out, 0, &digipump_squash,
                                      &digipump_pump, &digipump_warm, &digipump_mix };
        int x = pc_move(*vals[id], d);
        pc_set(view, ids[id], x);
        *vals[id] = x;                          /* shown at once; the render confirms */
        break;
    }
    }
    fw_invalidate(view);
    return 1;
}

/* ev_key, every key on every screen, never taken: FUNC and the knob presses
 * (40..47 = A..H) down or up (a release comes with flags 0 or 0x10) */
int pumpdt_evkey(void *brain, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12), down = (*(int32_t *)((char *)ev + 16) & 1) != 0;
    (void)brain;
    if (id == DT_K_FUNC)
        pc_func = down;
    else if (id >= DT_K_PUSH_A && id < DT_K_PUSH_A + 8)
        pc_push[id - DT_K_PUSH_A] = (unsigned char)down;
    return 0;
}

/* key(view, event), while the page is up: YES swaps the record and the graph.
 * Everything else is the stock's. */
static int32_t pumpdt_key(void *view, void *ev)
{
    int id = *(int32_t *)((char *)ev + 12);
    int flags = *(int32_t *)((char *)ev + 16);
    if (id != DT_K_YES)
        return 0;
    if ((flags & 1) && !(flags & 0x10) && !(flags & 8) && !pc_still()) {
        pc_graph = !pc_graph;
        pc_due = 0;
        fw_invalidate(view);
    }
    return 1;
}

/* CLEAR (FUNC + held LFO + PLAY): PUMP's defaults, PUMP off */
static void pumpdt_clear(void *view)
{
    pc_defaults(view, 0);
    fw_invalidate(view);
}

/* in the stock compressor's place: after it, and pc_drop_comp takes it out */
struct core_mpage pumpdt_page = {
    11, "PUMP", "Pump", pumpdt_draw, pumpdt_key, pumpdt_enc, 0, 0, pumpdt_clear
};
