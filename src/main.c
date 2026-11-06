/*
 * HTMLRD - reader for pages converted with html2ce.py
 *
 * Menu:   Up/Down choose a page, Enter or 2nd opens it, Clear quits.
 * Reader: Up/Down scroll a line (hold to repeat), 2nd / Alpha page down / up,
 *         Right / Left next / previous page of a long document,
 *         Clear back to the menu.
 *
 * Headings are drawn in three sizes, bold/italic/links get colours, images are
 * drawn from sprite AppVars made by html2ce.py.
 */

#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <tice.h>

#include "core.h"

#define MARGIN_X 4
#define BAR_H    12
#define VIEW_TOP (BAR_H + 2)
#define VIEW_BOT (240 - BAR_H)           /* first row below the text area */
#define VIEW_H   (VIEW_BOT - VIEW_TOP)

#define LIST_H    10
#define LIST_ROWS ((240 - 2 * BAR_H - 4) / LIST_H)
#define MAX_FILES 48

/* Palette: 0-215 a 6x6x6 colour cube (index = r*36 + g*6 + b, levels 0..5),
 * 216-255 forty greys ending in white. Must match html2ce.py. */
#define C_BLACK 0
#define C_WHITE 255
#define C_LINK  4   /* cube (0,0,4): dark blue  */
#define C_ITAL  108 /* cube (3,0,0): dark red   */
#define C_DEAD  235 /* mid grey: link to a page that isn't on the calculator */

#define REPEAT_DELAY 12 /* frames before a held Up/Down starts repeating */
#define REPEAT_STEP  2

#ifndef gfx_RGBTo1555
#define gfx_RGBTo1555(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 7) | (((g) & 0xF8) << 2) | (((b) & 0xF8) >> 3) \
                | (((g) & 0x04) << 13)))
#endif

enum { K_UP = 1, K_DOWN = 2, K_LEFT = 4, K_RIGHT = 8,
       K_ENTER = 16, K_CLEAR = 32, K_2ND = 64, K_ALPHA = 128 };

typedef struct {
    char name[9];
    char title[41];
} Entry;

static Entry    entries[MAX_FILES];
static Doc      doc;
static char     cur_name[9];
static uint16_t palette[256];

/* ---- input -------------------------------------------------------------------- */

static uint8_t poll_keys(void)
{
    uint8_t k = 0;

    kb_Scan();
    if (kb_Data[7] & kb_Up)    k |= K_UP;
    if (kb_Data[7] & kb_Down)  k |= K_DOWN;
    if (kb_Data[7] & kb_Left)  k |= K_LEFT;
    if (kb_Data[7] & kb_Right) k |= K_RIGHT;
    if (kb_Data[6] & kb_Enter) k |= K_ENTER;
    if (kb_Data[6] & kb_Clear) k |= K_CLEAR;
    if (kb_Data[1] & kb_2nd)   k |= K_2ND;
    if (kb_Data[2] & kb_Alpha) k |= K_ALPHA;
    return k;
}

/* Keys that went down since last call, plus auto-repeat for Up/Down. */
static uint8_t key_events(void)
{
    static uint8_t  prev;
    static uint16_t hold;
    uint8_t now = poll_keys();
    uint8_t ev = now & (uint8_t)~prev;

    if ((now & (K_UP | K_DOWN)) && now == prev) {
        if (hold < 60000u)
            hold++;
        if (hold >= REPEAT_DELAY && (hold - REPEAT_DELAY) % REPEAT_STEP == 0)
            ev |= now & (K_UP | K_DOWN);
    } else {
        hold = 0;
    }
    prev = now;
    return ev;
}

/* ---- files -------------------------------------------------------------------- */

/* Find every converted page that is the first page of its document. */
static uint8_t scan_pages(void)
{
    void   *pos = NULL;
    char   *name;
    uint8_t n = 0, h, i;

    while (n < MAX_FILES && (name = ti_Detect(&pos, "HR\002")) != NULL) {
        const uint8_t *p;
        uint16_t       size;

        h = ti_Open(name, "r");
        if (!h)
            continue;
        p = (const uint8_t *)ti_GetDataPtr(h);
        size = ti_GetSize(h);
        if (size >= 21 && p[2] == 2 && p[20] <= 40) {
            bool first = true;

            for (i = 4; i < 12; i++) /* skip continuation pages (they have a prev) */
                if (p[i])
                    first = false;
            if (first) {
                strncpy(entries[n].name, name, 8);
                entries[n].name[8] = 0;
                memcpy(entries[n].title, p + 21, p[20]);
                entries[n].title[p[20]] = 0;
                n++;
            }
        }
        ti_Close(h);
    }
    return n;
}

static bool load_page(const char *name)
{
    uint8_t h = ti_Open(name, "r");
    int     r;

    if (!h)
        return false;
    r = doc_layout(&doc, (const uint8_t *)ti_GetDataPtr(h), ti_GetSize(h));
    ti_Close(h);
    if (r != DOC_OK)
        return false;
    strncpy(cur_name, name, 8);
    cur_name[8] = 0;
    return true;
}

/* ---- drawing ------------------------------------------------------------------ */

static void init_palette(void)
{
    unsigned i, r, g, b, v;

    for (i = 0; i < 216; i++) {
        r = (i / 36) * 51;
        g = ((i / 6) % 6) * 51;
        b = (i % 6) * 51;
        palette[i] = gfx_RGBTo1555(r, g, b);
    }
    for (i = 216; i < 256; i++) {
        v = ((i - 216 + 1) * 255) / 40;
        palette[i] = gfx_RGBTo1555(v, v, v);
    }
    gfx_SetPalette(palette, sizeof palette, 0);
}

static void text_on_white(void)
{
    gfx_SetTextFGColor(C_BLACK);
    gfx_SetTextBGColor(C_WHITE);
    gfx_SetTextTransparentColor(C_WHITE);
}

static void text_on_black(void)
{
    gfx_SetTextFGColor(C_WHITE);
    gfx_SetTextBGColor(C_BLACK);
    gfx_SetTextTransparentColor(C_BLACK);
}

static void draw_chrome(const char *top, const char *bottom)
{
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle(0, 0, 320, BAR_H);
    gfx_FillRectangle(0, VIEW_BOT, 320, BAR_H);
    text_on_black();
    gfx_SetTextScale(1, 1);
    gfx_SetTextXY(MARGIN_X, 2);
    gfx_PrintString(top);
    gfx_SetTextXY(MARGIN_X, VIEW_BOT + 2);
    gfx_PrintString(bottom);
}

/* One character cell, optionally double-struck for bold. */
static void put_char(int x, int y, char c, bool bold)
{
    gfx_SetTextXY(x, y);
    gfx_PrintChar(c);
    if (bold) {
        gfx_SetTextXY(x + 1, y);
        gfx_PrintChar(c);
    }
}

static void draw_text_line(const LineView *v, int y)
{
    const uint8_t *p = v->p;
    uint8_t  nseg = *p++, s, len, st, head, k;
    uint16_t width = 0;
    int      x, sx, sy, color;
    bool     bold;

    for (s = 0, st = 0; s < nseg; s++) { /* first pass: total width, for centring */
        st = p[0];
        len = p[1];
        width += (uint16_t)(len * CHAR_W * head_sx(STYLE_HEAD(st)));
        p += 2 + len;
    }
    p = v->p + 1;
    x = MARGIN_X + v->indent;
    if (v->center && width < VIEW_W - v->indent)
        x += (VIEW_W - v->indent - width) / 2;

    for (s = 0; s < nseg; s++) {
        const uint8_t *text;
        int            x0 = x;

        st = *p++;
        len = *p++;
        text = p;
        p += len;
        head = STYLE_HEAD(st);
        sx = head_sx(head);
        sy = head_sy(head);
        bold = (st & S_BOLD) || head;
        color = (st & S_LINK) ? C_LINK : (st & S_DEAD) ? C_DEAD
              : (st & S_ITAL) ? C_ITAL : C_BLACK;

        gfx_SetTextFGColor(color);
        gfx_SetTextScale(sx, sy);
        for (k = 0; k < len; k++, x += CHAR_W * sx)
            if (text[k] != ' ')
                put_char(x, y + v->height - 1 - 8 * sy, (char)text[k], bold);
        if (st & (S_LINK | S_DEAD)) { /* underline */
            gfx_SetColor(color);
            gfx_HorizLine(x0, y + v->height - 1, x - x0);
        }
    }
    gfx_SetTextScale(1, 1);
    gfx_SetTextFGColor(C_BLACK);
}

static void draw_image(const LineView *v, int y)
{
    char name[9];
    int  x = MARGIN_X + v->indent, w = v->p[8], h = v->p[9];
    uint8_t slot;

    memcpy(name, v->p, 8);
    name[8] = 0;
    if (v->center && w < VIEW_W - v->indent)
        x += (VIEW_W - v->indent - w) / 2;
    slot = ti_Open(name, "r");
    if (slot) {
        gfx_Sprite((gfx_sprite_t *)ti_GetDataPtr(slot), x, y + 2);
        ti_Close(slot);
    } else { /* sprite AppVar missing: outline where it should be */
        gfx_SetColor(C_DEAD);
        gfx_Rectangle(x, y + 2, w, h);
    }
}

static void draw_doc(uint16_t top)
{
    char        status[41];
    LineView    v, t;
    uint16_t    i;
    int         y;

    gfx_FillScreen(C_WHITE);
    text_on_white();
    if (doc.nlines == 0) {
        gfx_SetTextXY(MARGIN_X, VIEW_TOP);
        gfx_PrintString("(empty page)");
    } else {
        doc_line(&doc, top, &t);
        for (i = top; i < doc.nlines; i++) {
            doc_line(&doc, i, &v);
            y = VIEW_TOP + (int)(v.y - t.y);
            if (y >= VIEW_BOT)
                break;
            if (v.kind == LK_IMAGE) {
                draw_image(&v, y); /* may run past the bottom: the bar covers it */
            } else if (y + v.height <= VIEW_BOT) {
                if (v.kind == LK_TEXT) {
                    draw_text_line(&v, y);
                } else {
                    gfx_SetColor(C_DEAD);
                    gfx_HorizLine(MARGIN_X + v.indent, y + 3, VIEW_W - v.indent);
                }
            }
        }
    }
    sprintf(status, "%s %u/%u%s%s%s", cur_name, (unsigned)(top + 1), (unsigned)doc.nlines,
            doc.prev[0] ? " <" : "", doc.next[0] ? " >" : "", doc.truncated ? " (cut)" : "");
    draw_chrome(doc.title[0] ? doc.title : cur_name, status);
}

static void draw_list(uint8_t count, uint8_t sel, uint8_t top, const char *msg)
{
    char    row[41];
    uint8_t i, k;
    const char *s;

    gfx_FillScreen(C_WHITE);
    text_on_white();
    if (!count) {
        gfx_PrintStringXY("No pages found.", MARGIN_X, VIEW_TOP);
        gfx_PrintStringXY("Convert HTML with html2ce.py,", MARGIN_X, VIEW_TOP + 2 * LINE_H);
        gfx_PrintStringXY("then send the .8xv files to", MARGIN_X, VIEW_TOP + 3 * LINE_H);
        gfx_PrintStringXY("the calculator.", MARGIN_X, VIEW_TOP + 4 * LINE_H);
    }
    for (i = 0; i < LIST_ROWS && top + i < count; i++) {
        const Entry *e = &entries[top + i];
        int y = BAR_H + 2 + i * LIST_H;

        k = 0;
        for (s = e->name; *s && k < 8; s++)
            row[k++] = *s;
        while (k < 9)
            row[k++] = ' ';
        for (s = e->title; *s && k < 39; s++)
            row[k++] = *s;
        row[k] = 0;
        if (top + i == sel) {
            gfx_SetColor(C_BLACK);
            gfx_FillRectangle(0, y - 1, 320, LIST_H);
            text_on_black();
        } else {
            text_on_white();
        }
        gfx_PrintStringXY(row, MARGIN_X, y);
    }
    if (msg) {
        text_on_white();
        gfx_PrintStringXY(msg, MARGIN_X, VIEW_BOT - LINE_H - 2);
    }
    draw_chrome("HTML Reader", count ? "Enter/2nd: open   Clear: quit" : "Clear: quit");
}

/* ---- main --------------------------------------------------------------------- */

static uint16_t clamp_top(uint16_t t, uint16_t max_top)
{
    return t > max_top ? max_top : t;
}

int main(void)
{
    enum { S_LIST, S_DOC } state = S_LIST;
    uint8_t     count, sel = 0, top_row = 0, ev;
    uint16_t    top = 0, max_top = 0, t;
    bool        dirty = true;
    const char *msg = NULL;

    count = scan_pages();

    gfx_Begin();
    gfx_SetDrawBuffer();
    init_palette();
    gfx_SetMonospaceFont(8);

    for (;;) {
        ev = key_events();

        if (state == S_LIST) {
            if (ev & K_CLEAR)
                break;
            if (count) {
                if ((ev & K_DOWN) && sel + 1 < count) { sel++; dirty = true; }
                if ((ev & K_UP) && sel > 0)           { sel--; dirty = true; }
                if (sel < top_row)
                    top_row = sel;
                if (sel >= top_row + LIST_ROWS)
                    top_row = sel - LIST_ROWS + 1;
                if (ev & (K_ENTER | K_2ND)) {
                    if (load_page(entries[sel].name)) {
                        state = S_DOC;
                        top = 0;
                        max_top = doc_max_top(&doc, VIEW_H);
                        msg = NULL;
                    } else {
                        msg = "Could not open that page.";
                    }
                    dirty = true;
                }
            }
            if (dirty)
                draw_list(count, sel, top_row, msg);
        } else {
            if ((ev & K_DOWN) && top < max_top)  { top++; dirty = true; }
            if ((ev & K_UP) && top > 0)          { top--; dirty = true; }
            if (ev & K_2ND) {                    /* page down, keeping one line of overlap */
                t = clamp_top(doc_line_at(&doc, doc_y(&doc, top) + VIEW_H - LINE_H), max_top);
                top = (t == top) ? clamp_top(top + 1, max_top) : t;
                dirty = true;
            }
            if (ev & K_ALPHA) {                  /* page up */
                uint16_t y = doc_y(&doc, top);

                t = doc_line_at(&doc, y > VIEW_H - LINE_H ? y - (VIEW_H - LINE_H) : 0);
                top = (t >= top && top > 0) ? top - 1 : t;
                dirty = true;
            }
            if (((ev & K_RIGHT) && doc.next[0]) || ((ev & K_LEFT) && doc.prev[0])) {
                char name[9];

                strcpy(name, (ev & K_RIGHT) ? doc.next : doc.prev);
                if (load_page(name)) {
                    top = 0;
                    max_top = doc_max_top(&doc, VIEW_H);
                } else {
                    state = S_LIST;
                    msg = "Could not open that page.";
                }
                dirty = true;
            }
            if (ev & K_CLEAR) {
                state = S_LIST;
                dirty = true;
            }
            if (dirty) {
                if (state == S_DOC)
                    draw_doc(top);
                else
                    draw_list(count, sel, top_row, msg);
            }
        }

        if (dirty) {
            gfx_SwapDraw();
            dirty = false;
        }
        delay(15);
    }

    gfx_End();
    return 0;
}
