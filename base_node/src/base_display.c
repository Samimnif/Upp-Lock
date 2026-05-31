/*
 * base_display.c  —  Upp-Lock base node display
 *
 * Target : 240×240 GC9A01 round TFT, RGB565, Zephyr gc9x01x SPI driver
 * Render : raw display_write(), no LVGL
 *
 * Design decisions for physical readability on 1.28":
 *   - Minimum text scale 2 (10×14 px glyphs) for labels
 *   - Scale 3 (15×21 px) for primary values
 *   - High-contrast colours only: white/bright-teal/bright-red on near-black
 *   - State banner fills full width (rect, not arc) — arcs via display API
 *     look poor without antialiasing; a solid chord band reads clearly
 *   - Sensor blocks split left/right of a centre lock icon
 *   - Trigger badges at bottom use filled rects with strong border contrast
 */

#include "base_display.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* ── Display geometry ─────────────────────────────────── */
#define DISPLAY_NODE  DT_CHOSEN(zephyr_display)
#define LCD_W  240
#define LCD_H  240
#define CX     120
#define CY     120

/* ── RGB565 macro ─────────────────────────────────────── */
#define RGB565(r,g,b) \
    ((uint16_t)((((r)&0xF8u)<<8)|(((g)&0xFCu)<<3)|((b)>>3)))

/*
 * Palette — all colours validated for visibility on IPS TFT:
 *   • Background is not pure black so the backlight doesn't blow out whites
 *   • State colours are at full saturation — no dim variants on-screen
 *   • Label text is mid-brightness, value text is near-white
 */
#define C_BG        RGB565( 10,  10,  20)   /* dark blue-black             */
#define C_DIVIDER   RGB565( 30,  40,  60)   /* subtle separator lines      */

/* State banner colours — solid, full saturation */
#define C_TEAL      RGB565(  0, 220, 160)   /* ARMED  — bright teal        */
#define C_TEAL_BG   RGB565(  0,  50,  36)   /* ARMED banner background     */
#define C_BLUE      RGB565( 60, 140, 255)   /* DISARMED — bright blue      */
#define C_BLUE_BG   RGB565(  8,  28,  70)   /* DISARMED banner background  */
#define C_RED       RGB565(255,  40,  60)   /* ALARM  — bright red         */
#define C_RED_BG    RGB565( 70,   6,  12)   /* ALARM banner background     */

/* Data value colours — bright enough to read at arm's length */
#define C_WHITE     RGB565(240, 245, 255)   /* primary values              */
#define C_CYAN      RGB565( 80, 230, 255)   /* light sensor value          */
#define C_ACC_VAL   RGB565(140, 255, 200)   /* accel values (mint green)   */
#define C_GYR_VAL   RGB565(160, 170, 255)   /* gyro values (lavender)      */
#define C_HALL_VAL  RGB565(200, 220, 255)   /* hall value (ice blue)       */
#define C_AMBER     RGB565(255, 185,   0)   /* triggered badge / warning   */
#define C_AMBER_BG  RGB565( 45,  28,   0)   /* triggered badge bg          */

/* Label / dim text — still clearly visible, just secondary */
#define C_LABEL     RGB565(100, 140, 160)   /* section titles              */
#define C_HINT      RGB565( 60,  80, 100)   /* GP20 hint at bottom         */
#define C_BADGE_OFF RGB565( 25,  35,  55)   /* inactive badge fill         */
#define C_BADGE_BDR RGB565( 50,  70,  100)  /* inactive badge border       */

/* ── Globals ───────────────────────────────────────────── */
static const struct device *display_dev;
static bool display_ready;

/* ── Scan-line draw buffer (one row at a time) ─────────── */
static uint16_t linebuf[LCD_W];

static void rect(int x, int y, int w, int h, uint16_t color)
{
    if (!display_ready || w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= LCD_W || y >= LCD_H) return;
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;

    for (int i = 0; i < w; i++) linebuf[i] = color;
    struct display_buffer_descriptor d = {
        .buf_size = (size_t)w * 2u,
        .width    = (uint16_t)w,
        .height   = 1,
        .pitch    = (uint16_t)w,
    };
    for (int row = 0; row < h; row++)
        display_write(display_dev, x, y + row, &d, linebuf);
}

/* horizontal line helper */
static void hline(int x, int y, int w, uint16_t color)
{
    rect(x, y, w, 1, color);
}

/* ── 5×7 bitmap font ───────────────────────────────────── */
struct glyph { char c; uint8_t col[5]; };
static const struct glyph font5x7[] = {
    {' ',{0x00,0x00,0x00,0x00,0x00}},
    {'-',{0x08,0x08,0x08,0x08,0x08}},
    {'+',{0x08,0x08,0x3E,0x08,0x08}},
    {'.',{0x00,0x60,0x60,0x00,0x00}},
    {':',{0x00,0x36,0x36,0x00,0x00}},
    {'0',{0x3E,0x51,0x49,0x45,0x3E}},
    {'1',{0x00,0x42,0x7F,0x40,0x00}},
    {'2',{0x42,0x61,0x51,0x49,0x46}},
    {'3',{0x21,0x41,0x45,0x4B,0x31}},
    {'4',{0x18,0x14,0x12,0x7F,0x10}},
    {'5',{0x27,0x45,0x45,0x45,0x39}},
    {'6',{0x3C,0x4A,0x49,0x49,0x30}},
    {'7',{0x01,0x71,0x09,0x05,0x03}},
    {'8',{0x36,0x49,0x49,0x49,0x36}},
    {'9',{0x06,0x49,0x49,0x29,0x1E}},
    {'A',{0x7E,0x11,0x11,0x11,0x7E}},
    {'B',{0x7F,0x49,0x49,0x49,0x36}},
    {'C',{0x3E,0x41,0x41,0x41,0x22}},
    {'D',{0x7F,0x41,0x41,0x22,0x1C}},
    {'E',{0x7F,0x49,0x49,0x49,0x41}},
    {'F',{0x7F,0x09,0x09,0x09,0x01}},
    {'G',{0x3E,0x41,0x49,0x49,0x7A}},
    {'H',{0x7F,0x08,0x08,0x08,0x7F}},
    {'I',{0x00,0x41,0x7F,0x41,0x00}},
    {'J',{0x20,0x40,0x41,0x3F,0x01}},
    {'K',{0x7F,0x08,0x14,0x22,0x41}},
    {'L',{0x7F,0x40,0x40,0x40,0x40}},
    {'M',{0x7F,0x02,0x0C,0x02,0x7F}},
    {'N',{0x7F,0x04,0x08,0x10,0x7F}},
    {'O',{0x3E,0x41,0x41,0x41,0x3E}},
    {'P',{0x7F,0x09,0x09,0x09,0x06}},
    {'Q',{0x3E,0x41,0x51,0x21,0x5E}},
    {'R',{0x7F,0x09,0x19,0x29,0x46}},
    {'S',{0x46,0x49,0x49,0x49,0x31}},
    {'T',{0x01,0x01,0x7F,0x01,0x01}},
    {'U',{0x3F,0x40,0x40,0x40,0x3F}},
    {'V',{0x1F,0x20,0x40,0x20,0x1F}},
    {'W',{0x3F,0x40,0x38,0x40,0x3F}},
    {'X',{0x63,0x14,0x08,0x14,0x63}},
    {'Y',{0x07,0x08,0x70,0x08,0x07}},
    {'Z',{0x61,0x51,0x49,0x45,0x43}},
    {'a',{0x20,0x54,0x54,0x54,0x78}},
    {'c',{0x38,0x44,0x44,0x44,0x20}},
    {'d',{0x38,0x44,0x44,0x48,0x7F}},
    {'e',{0x38,0x54,0x54,0x54,0x18}},
    {'g',{0x08,0x54,0x54,0x54,0x3C}},
    {'h',{0x7F,0x08,0x04,0x04,0x78}},
    {'i',{0x00,0x44,0x7D,0x40,0x00}},
    {'l',{0x00,0x41,0x7F,0x40,0x00}},
    {'m',{0x7C,0x04,0x18,0x04,0x78}},
    {'n',{0x7C,0x08,0x04,0x04,0x78}},
    {'o',{0x38,0x44,0x44,0x44,0x38}},
    {'r',{0x7C,0x08,0x04,0x04,0x08}},
    {'s',{0x48,0x54,0x54,0x54,0x20}},
    {'t',{0x04,0x3F,0x44,0x40,0x20}},
    {'u',{0x3C,0x40,0x40,0x20,0x7C}},
    {'w',{0x3C,0x40,0x30,0x40,0x3C}},
    {'x',{0x44,0x28,0x10,0x28,0x44}},
    {'y',{0x0C,0x50,0x50,0x50,0x3C}},
};

static const uint8_t *glyph_for(char c)
{
    for (size_t i = 0; i < ARRAY_SIZE(font5x7); i++)
        if (font5x7[i].c == c) return font5x7[i].col;
    return font5x7[0].col;
}

/*
 * Draw a string at (x,y), scale pixels per dot, colour on bg.
 * Glyph cell = (5+1)*scale wide, 7*scale tall.
 */
static void text(int x, int y, const char *s,
                 uint16_t color, uint16_t bg, int scale)
{
    if (!display_ready || scale < 1) return;
    for (; *s; s++) {
        const uint8_t *g = glyph_for(*s);
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                uint16_t c = (g[col] & BIT(row)) ? color : bg;
                rect(x + col*scale, y + row*scale, scale, scale, c);
            }
        }
        rect(x + 5*scale, y, scale, 7*scale, bg); /* inter-char gap */
        x += 6 * scale;
    }
}

/* centred text */
static void ctext(int y, const char *s, uint16_t fg, uint16_t bg, int scale)
{
    int w = (int)strlen(s) * 6 * scale;
    text((LCD_W - w) / 2, y, s, fg, bg, scale);
}

/* right-aligned text */
static void rtext(int rx, int y, const char *s,
                  uint16_t fg, uint16_t bg, int scale)
{
    int w = (int)strlen(s) * 6 * scale;
    text(rx - w, y, s, fg, bg, scale);
}

/* ── Chord-clipped banner ──────────────────────────────────────────────────
 * Draws a horizontal filled band from y0..y1 clipped to the circle of
 * radius R centred at (CX,CY).  Gives the "arc band" look without any
 * trigonometry or pixel-by-pixel loops.
 */
static void chord_band(int y0, int y1, uint16_t color)
{
    const int R  = 118;   /* clip radius — 1 px inside the 240 px circle */
    const int R2 = R * R;
    for (int y = y0; y <= y1; y++) {
        int dy  = y - CY;
        int dx2 = R2 - dy*dy;
        if (dx2 < 0) continue;
        /* integer sqrt via bisection — runs ≤17 iterations */
        int dx = R;
        while (dx * dx > dx2) dx--;
        int x0 = CX - dx, x1_v = CX + dx;
        if (x0 < 0)     x0   = 0;
        if (x1_v >= LCD_W) x1_v = LCD_W - 1;
        if (x1_v >= x0)
            rect(x0, y, x1_v - x0 + 1, 1, color);
    }
}

/* ── Lock icon  (pixel art, 20×24 at given top-left) ──────────────────────*/
static void draw_lock(int cx2, int top_y, uint16_t color, bool open)
{
    /*
     * Shackle: two vertical lines + top arc approximated as 3-pixel top.
     * Body: 20×13 rectangle with a keyhole.
     */
    int lx = cx2 - 10;   /* left edge of body */
    int sh_top = top_y;
    int body_y = sh_top + 11;

    /* shackle */
    int sl = lx + 3, sr = lx + 16;
    if (open) {
        /* right arm lifted off body */
        for (int y = sh_top; y < sh_top + 7; y++) rect(sl, y, 2, 1, color);
        hline(sl, sh_top, sr - sl + 2, color);
        hline(sl, sh_top+1, sr - sl + 2, color);
        /* right pillar stops 4px above body */
        for (int y = body_y - 4; y < body_y; y++) rect(sr, y, 2, 1, color);
    } else {
        /* both pillars go from sh_top down to body */
        for (int y = sh_top; y < body_y; y++) {
            rect(sl, y, 2, 1, color);
            rect(sr, y, 2, 1, color);
        }
        hline(sl, sh_top, sr - sl + 2, color);
        hline(sl, sh_top+1, sr - sl + 2, color);
    }

    /* body outline */
    int bw = 20, bh = 13;
    hline(lx,      body_y,        bw, color);
    hline(lx,      body_y+bh-1,   bw, color);
    for (int y = body_y; y < body_y+bh; y++) {
        rect(lx,      y, 1, 1, color);
        rect(lx+bw-1, y, 1, 1, color);
    }

    /* keyhole — circle + slot */
    rect(cx2-2, body_y+3, 5, 4, color);   /* rough circle */
    rect(cx2-1, body_y+6, 3, 4, color);   /* slot down    */
}

/* ── Bordered badge (36×16) ────────────────────────────── */
static void badge(int cx2, int cy2, const char *label, bool active)
{
    uint16_t fill = active ? C_AMBER_BG  : C_BADGE_OFF;
    uint16_t bdr  = active ? C_AMBER     : C_BADGE_BDR;
    uint16_t fg   = active ? C_AMBER     : C_LABEL;

    int x = cx2 - 18, y = cy2 - 8;
    rect(x, y, 36, 16, fill);
    hline(x,    y,    36, bdr);
    hline(x,    y+15, 36, bdr);
    for (int i = 0; i < 16; i++) {
        rect(x,    y+i, 1, 1, bdr);
        rect(x+35, y+i, 1, 1, bdr);
    }
    /* label centred, scale 1 — badges are tight */
    int lw = (int)strlen(label) * 6;
    text(cx2 - lw/2, cy2 - 3, label, fg, fill, 1);
}

/* ── IMU sub-block (label + X/Y/Z rows) ───────────────────
 * lx,ty = top-left corner; field width = 46 px at scale 1 (labels+value)
 * scale 1 for labels, scale 2 for numeric values
 */
static void imu_block(int lx, int ty,
                      const char *title, uint16_t title_col,
                      int vx, int vy, int vz,
                      uint16_t val_col, bool triggered)
{
    uint16_t tc  = triggered ? C_AMBER : title_col;
    uint16_t vc  = triggered ? C_AMBER : val_col;
    char buf[8];

    /* title label at scale 2 */
    text(lx, ty, title, tc, C_BG, 2);

    /* thin divider */
    hline(lx, ty + 15, 56, C_DIVIDER);

    /* X row */
    text(lx,    ty+19, "X", C_LABEL, C_BG, 1);
    snprintf(buf, sizeof(buf), "%+5d", vx);
    text(lx+8,  ty+19, buf, vc, C_BG, 1);

    /* Y row */
    text(lx,    ty+29, "Y", C_LABEL, C_BG, 1);
    snprintf(buf, sizeof(buf), "%+5d", vy);
    text(lx+8,  ty+29, buf, vc, C_BG, 1);

    /* Z row */
    text(lx,    ty+39, "Z", C_LABEL, C_BG, 1);
    snprintf(buf, sizeof(buf), "%+5d", vz);
    text(lx+8,  ty+39, buf, vc, C_BG, 1);
}

/* ── Public API ─────────────────────────────────────────── */

int base_display_init(void)
{
    display_dev = DEVICE_DT_GET(DISPLAY_NODE);
    if (!device_is_ready(display_dev)) {
        printk("[DISPLAY] Device not ready\n");
        display_ready = false;
        return -ENODEV;
    }

    display_blanking_off(display_dev);
    display_ready = true;

    rect(0, 0, LCD_W, LCD_H, C_BG);

    /* Splash — scale 3 is 15×21 px per glyph, clearly legible */
    ctext(88,  "UPP",  C_TEAL,  C_BG, 3);
    ctext(114, "LOCK", C_WHITE, C_BG, 3);

    k_sleep(K_MSEC(700));
    return 0;
}

void base_display_update(const struct base_display_values *v)
{
    if (!display_ready || v == NULL) return;

    /* Pick state palette */
    uint16_t s_fg, s_bg;
    const char *s_str;
    if (v->alarm_active) {
        s_fg = C_RED;  s_bg = C_RED_BG;  s_str = "ALARM";
    } else if (v->armed) {
        s_fg = C_TEAL; s_bg = C_TEAL_BG; s_str = "ARMED";
    } else {
        s_fg = C_BLUE; s_bg = C_BLUE_BG; s_str = "DISARMED";
    }

    /* ── Clear everything ── */
    rect(0, 0, LCD_W, LCD_H, C_BG);

    /*
     * Layout (y coordinates, 240 px tall):
     *
     *   y=  0..27   State banner (chord-clipped)
     *   y= 30..44   LIGHT label + value
     *   y= 48       horizontal divider
     *   y= 50..105  IMU blocks left (ACC) + right (GYR) + lock icon centre
     *   y=108       horizontal divider
     *   y=112..130  HALL label + value
     *   y=134       horizontal divider
     *   y=138..162  Trigger badges
     *   y=168..178  GP20 hint
     *
     * Safe x zone inside 240px circle at these y values is ~30..210.
     * All content respects that margin.
     */

    /* ── 1. State banner ── */
    chord_band(0, 27, s_bg);
    /* bright 2-px bottom edge of banner */
    hline(0, 26, LCD_W, s_fg);
    hline(0, 27, LCD_W, s_fg);
    /* state label centred, scale 2 (12×14 px glyphs) */
    ctext(6, s_str, s_fg, s_bg, 2);

    /* ── 2. LIGHT reading ── */
    {
        char buf[10];
        /* label scale 1, value scale 2 */
        ctext(32, "LIGHT", C_LABEL, C_BG, 1);
        snprintf(buf, sizeof(buf), "%u", v->light_raw);
        ctext(42, buf, C_CYAN, C_BG, 2);
    }

    /* ── 3. Horizontal divider ── */
    hline(30, 60, 180, C_DIVIDER);

    /* ── 4. Left IMU block — ACC  (x=30..85) ── */
    imu_block(30, 64,
              "ACC", C_LABEL,
              v->ax, v->ay, v->az,
              C_ACC_VAL, v->accel_theft);

    /* ── 5. Lock icon (centre) ── */
    draw_lock(CX, 68, s_fg, v->alarm_active);

    /* ── 6. Right IMU block — GYR (x=154..209) ── */
    imu_block(154, 64,
              "GYR", C_LABEL,
              v->gx, v->gy, v->gz,
              C_GYR_VAL, v->gyro_theft);

    /* ── 7. Horizontal divider ── */
    hline(30, 112, 180, C_DIVIDER);

    /* ── 8. HALL reading ── */
    {
        char buf[10];
        uint16_t hc = v->hall_theft ? C_AMBER : C_HALL_VAL;
        ctext(116, "HALL", C_LABEL, C_BG, 1);
        snprintf(buf, sizeof(buf), "%+d", (int)v->hall);
        ctext(126, buf, hc, C_BG, 2);
    }

    /* ── 9. Horizontal divider ── */
    hline(30, 145, 180, C_DIVIDER);

    /* ── 10. Trigger badges ── */
    badge( 70, 158, "ACC", v->accel_theft);
    badge(120, 158, "GYR", v->gyro_theft);
    badge(170, 158, "MAG", v->hall_theft);

    /* ── 11. GP20 hint ── */
    ctext(172, v->armed ? "GP20 DISARM" : "GP20 ARM",
          C_HINT, C_BG, 1);
}