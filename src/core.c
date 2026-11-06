#include "core.h"

#include <string.h>

/* Token opcodes - must match html2ce.py */
enum {
    T_END, T_HEAD, T_PARA, T_BR, T_CENTER, T_LINK, T_LINK_END,
    T_BOLD, T_ITALIC, T_IMAGE, T_INDENT, T_PRE, T_RULE
};

#define FORMAT_VERSION 2
#define MAXC           48    /* more than VIEW_W / CHAR_W, so a line always fits */
#define Y_LIMIT        60000u

typedef struct {
    uint16_t off; /* where the line starts in arena[] */
    uint16_t y;   /* pixel position of the line       */
} LineRec;

/* Each line: [flags: bit0 centre, bits1-2 kind][indent px][height px][payload] */
static uint8_t arena[ARENA_CAP];
static LineRec recs[LINE_CAP];

typedef struct {
    Doc     *doc;
    uint16_t arena_len, nlines, y;

    char     cur[MAXC];  /* line being built, one style byte per char */
    uint8_t  cst[MAXC];
    uint8_t  clen;
    uint16_t cpx;        /* pixel width of cur                        */
    bool     has_text;
    bool     line_center;
    uint8_t  line_ind;

    char     word[MAXC]; /* word being collected                      */
    uint8_t  wst[MAXC];
    uint8_t  wlen;
    uint16_t wpx;

    uint8_t  ind;        /* current indent in pixels                  */
    uint8_t  head;       /* current heading size 0-3                  */
    uint8_t  link;       /* 0 none, 1 live, 2 dead                    */
    bool     bold, ital, center, pre;
    bool     last_blank, full;
} Layout;

/* ---- line storage ---------------------------------------------------------------- */

/* Reserve room for a line; returns where to write it, or NULL when full. */
static uint8_t *reserve(Layout *l, uint32_t bytes, uint8_t height)
{
    uint8_t *p;

    if (l->full)
        return NULL;
    if (l->nlines >= LINE_CAP || (uint32_t)l->arena_len + bytes > ARENA_CAP
            || (uint32_t)l->y + height > Y_LIMIT) {
        l->full = true;
        l->doc->truncated = true;
        return NULL;
    }
    recs[l->nlines].off = l->arena_len;
    recs[l->nlines].y = l->y;
    l->nlines++;
    l->y += height;
    p = arena + l->arena_len;
    l->arena_len += (uint16_t)bytes;
    return p;
}

static uint8_t cur_style(const Layout *l)
{
    return (uint8_t)((l->bold ? S_BOLD : 0) | (l->ital ? S_ITAL : 0)
                     | (l->head << 2)
                     | (l->link == 1 ? S_LINK : 0) | (l->link == 2 ? S_DEAD : 0));
}

static void commit_text(Layout *l)
{
    uint8_t  n = l->clen, i, j, nseg = 0, h = LINE_H, st;
    uint32_t size = 4; /* header (3) + segment count (1) */
    uint8_t *p;

    while (n && l->cur[n - 1] == ' ')
        n--;
    for (i = 0; i < n; i++) {
        uint8_t hh = (uint8_t)(8 * head_sy(STYLE_HEAD(l->cst[i])) + 1);

        if (hh > h)
            h = hh;
        if (i == 0 || l->cst[i] != l->cst[i - 1]) {
            nseg++;
            size += 2;
        }
    }
    size += n;
    p = reserve(l, size, h);
    if (!p)
        return;
    *p++ = (uint8_t)((LK_TEXT << 1) | (l->line_center ? 1 : 0));
    *p++ = l->line_ind;
    *p++ = h;
    *p++ = nseg;
    for (i = 0; i < n; i = j) {
        st = l->cst[i];
        for (j = i; j < n && l->cst[j] == st; j++)
            ;
        *p++ = st;
        *p++ = (uint8_t)(j - i);
        memcpy(p, l->cur + i, j - i);
        p += j - i;
    }
    l->last_blank = (n == 0);
}

static void start_line(Layout *l)
{
    l->clen = 0;
    l->cpx = 0;
    l->has_text = false;
}

static void newline(Layout *l)
{
    commit_text(l);
    start_line(l);
}

static void emit_blank(Layout *l)
{
    uint8_t *p = reserve(l, 4, LINE_H);

    if (!p)
        return;
    p[0] = (uint8_t)(LK_TEXT << 1);
    p[1] = 0;
    p[2] = LINE_H;
    p[3] = 0;
    l->last_blank = true;
}

/* ---- words ----------------------------------------------------------------------- */

static void flush_word(Layout *l)
{
    uint16_t avail = (uint16_t)(VIEW_W - l->ind);
    uint16_t spx = 0;
    uint8_t  sst = 0;

    if (!l->wlen)
        return;
    if (l->has_text) {
        /* the space between two words: wide as the next word's size, and it
         * keeps bold/italic/link only when both neighbours have it */
        sst = (uint8_t)((l->cst[l->clen - 1] & l->wst[0] & ~S_HEAD_MASK)
                        | (l->wst[0] & S_HEAD_MASK));
        spx = (uint16_t)(CHAR_W * head_sx(STYLE_HEAD(sst)));
        if (l->cpx + spx + l->wpx > avail) {
            newline(l);
            spx = 0;
        }
    }
    if (!l->has_text) {
        l->line_center = l->center;
        l->line_ind = l->ind;
    }
    if (spx) {
        l->cur[l->clen] = ' ';
        l->cst[l->clen] = sst;
        l->clen++;
        l->cpx += spx;
    }
    memcpy(l->cur + l->clen, l->word, l->wlen);
    memcpy(l->cst + l->clen, l->wst, l->wlen);
    l->clen += l->wlen;
    l->cpx += l->wpx;
    l->has_text = true;
    l->wlen = 0;
    l->wpx = 0;
}

static void add_char(Layout *l, char c)
{
    uint8_t  st = cur_style(l);
    uint16_t cw = (uint16_t)(CHAR_W * head_sx(STYLE_HEAD(st)));
    uint16_t avail = (uint16_t)(VIEW_W - l->ind);

    if (l->pre) { /* keep every space, wrap hard at the edge */
        if (l->cpx + cw > avail || l->clen >= MAXC)
            newline(l);
        if (!l->has_text) {
            l->line_center = l->center;
            l->line_ind = l->ind;
        }
        l->cur[l->clen] = c;
        l->cst[l->clen] = st;
        l->clen++;
        l->cpx += cw;
        l->has_text = true;
        return;
    }
    if (c == ' ') {
        flush_word(l);
        return;
    }
    if (l->wpx + cw > avail || l->wlen >= MAXC - 1) { /* one word wider than a line */
        flush_word(l);
        newline(l);
    }
    l->word[l->wlen] = c;
    l->wst[l->wlen] = st;
    l->wlen++;
    l->wpx += cw;
}

static void add_str(Layout *l, const char *s)
{
    while (*s)
        add_char(l, *s++);
}

/* ---- breaks and blocks ----------------------------------------------------------- */

static void end_line(Layout *l) /* <br> */
{
    flush_word(l);
    if (l->nlines == 0 && !l->has_text) { /* ignore leading blank lines */
        start_line(l);
        return;
    }
    newline(l);
}

static void end_para(Layout *l) /* finish the line, leave a blank one, end any heading */
{
    flush_word(l);
    if (l->has_text)
        newline(l);
    if (l->nlines > 0 && !l->last_blank)
        emit_blank(l);
    l->head = 0;
}

static void rule(Layout *l)
{
    uint8_t *p;

    flush_word(l);
    if (l->has_text)
        newline(l);
    p = reserve(l, 3, 7);
    if (!p)
        return;
    p[0] = (uint8_t)(LK_RULE << 1);
    p[1] = l->ind;
    p[2] = 7;
    l->last_blank = false;
}

static void image(Layout *l, const uint8_t *name, uint8_t w, uint8_t h)
{
    uint8_t *p;
    uint16_t hh = (uint16_t)(h + 4);

    flush_word(l);
    if (l->has_text)
        newline(l);
    p = reserve(l, 13, hh > 255 ? 255 : (uint8_t)hh);
    if (!p)
        return;
    p[0] = (uint8_t)((LK_IMAGE << 1) | (l->center ? 1 : 0));
    p[1] = l->ind;
    p[2] = hh > 255 ? 255 : (uint8_t)hh;
    memcpy(p + 3, name, 8);
    p[11] = w;
    p[12] = h;
    l->last_blank = false;
}

/* ---- public API ------------------------------------------------------------------ */

static void copy_name(char *dst, const uint8_t *src)
{
    uint8_t i;

    for (i = 0; i < 8 && src[i]; i++)
        dst[i] = (char)src[i];
    dst[i] = 0;
}

static bool nonzero8(const uint8_t *p)
{
    uint8_t i;

    for (i = 0; i < 8; i++)
        if (p[i])
            return true;
    return false;
}

int doc_layout(Doc *d, const uint8_t *data, uint16_t size)
{
    static Layout l; /* big, so keep it off the stack */
    uint32_t pos;
    uint8_t  tl, c, n, w, h, i;

    memset(d, 0, sizeof *d);
    if (size < 21)
        return DOC_BAD_HEADER;
    if (data[0] != 'H' || data[1] != 'R')
        return DOC_BAD_MAGIC;
    if (data[2] != FORMAT_VERSION)
        return DOC_BAD_VERSION;
    tl = data[20];
    if (tl > 40 || 21u + tl > size)
        return DOC_BAD_HEADER;
    copy_name(d->prev, data + 4);
    copy_name(d->next, data + 12);
    memcpy(d->title, data + 21, tl);
    d->title[tl] = 0;
    pos = 21u + tl;

    memset(&l, 0, sizeof l);
    l.doc = d;
    l.last_blank = false;
    start_line(&l);

#define ARG() (pos < size ? data[pos++] : 0)

    while (pos < size && !l.full) {
        c = data[pos++];
        if (c >= 0x20) {
            add_char(&l, (char)c);
            continue;
        }
        switch (c) {
        case T_END:
            goto done;
        case T_HEAD:
            flush_word(&l);
            l.head = ARG() & 3;
            break;
        case T_PARA:
            end_para(&l);
            break;
        case T_BR:
            end_line(&l);
            break;
        case T_CENTER:
            l.center = ARG() != 0;
            break;
        case T_BOLD:
            l.bold = ARG() != 0;
            break;
        case T_ITALIC:
            l.ital = ARG() != 0;
            break;
        case T_LINK:
            if (pos + 8 > size) {
                pos = size;
                break;
            }
            l.link = nonzero8(data + pos) ? 1 : 2;
            pos += 8;
            break;
        case T_LINK_END:
            l.link = 0;
            break;
        case T_IMAGE:
            if (pos + 11 > size) {
                pos = size;
                break;
            }
            w = data[pos + 8];
            h = data[pos + 9];
            n = data[pos + 10];
            if (nonzero8(data + pos)) {
                image(&l, data + pos, w, h);
                pos += 11u + n;
            } else { /* no sprite: show the alt text instead */
                pos += 11;
                add_str(&l, n ? "[image: " : "[image]");
                for (i = 0; i < n && pos < size; i++)
                    add_char(&l, (char)data[pos++]);
                if (n)
                    add_str(&l, "]");
            }
            break;
        case T_INDENT:
            flush_word(&l);
            n = ARG();
            l.ind = (uint8_t)(16 * (n > 3 ? 3 : n));
            break;
        case T_PRE:
            flush_word(&l);
            l.pre = ARG() != 0;
            break;
        case T_RULE:
            rule(&l);
            break;
        default:
            break;
        }
    }
done:
    flush_word(&l);
    if (l.has_text)
        commit_text(&l);
    d->nlines = l.nlines;
    d->total_h = l.y;
    d->arena_used = l.arena_len;
    return DOC_OK;
}

bool doc_line(const Doc *d, uint16_t i, LineView *v)
{
    const uint8_t *p;

    if (i >= d->nlines)
        return false;
    p = arena + recs[i].off;
    v->y = recs[i].y;
    v->center = p[0] & 1;
    v->kind = (p[0] >> 1) & 3;
    v->indent = p[1];
    v->height = p[2];
    v->p = p + 3;
    return true;
}

uint16_t doc_y(const Doc *d, uint16_t i)
{
    return i < d->nlines ? recs[i].y : d->total_h;
}

uint16_t doc_line_at(const Doc *d, uint16_t y)
{
    uint16_t i;

    for (i = 0; i < d->nlines; i++)
        if (recs[i].y >= y)
            return i;
    return d->nlines ? (uint16_t)(d->nlines - 1) : 0;
}

uint16_t doc_max_top(const Doc *d, uint16_t view_height)
{
    uint16_t i;

    for (i = 0; i < d->nlines; i++)
        if (d->total_h - recs[i].y <= view_height)
            return i;
    return d->nlines ? (uint16_t)(d->nlines - 1) : 0;
}
