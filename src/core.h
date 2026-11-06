/*
 * core.h - page loading and layout for the HTML reader.
 *
 * Pure C with no calculator dependencies, so it can be tested on a PC.
 * Reads the page format produced by html2ce.py (format version 2) and turns it
 * into styled, word-wrapped lines with pixel positions.
 */
#ifndef CORE_H
#define CORE_H

#include <stdbool.h>
#include <stdint.h>

#define ARENA_CAP 30000u /* bytes of laid-out lines kept in RAM per page */
#define LINE_CAP  2500u  /* max lines per page                           */

#define VIEW_W 312 /* width of the text area in pixels             */
#define CHAR_W 8   /* width of one character at scale 1            */
#define LINE_H 9   /* height of a normal text line, in pixels      */

/* Per-run style byte used in text lines. */
#define S_BOLD       0x01
#define S_ITAL       0x02
#define S_HEAD_MASK  0x0C /* heading size 0-3 in bits 2-3 */
#define S_LINK       0x10 /* link whose target is on the calculator */
#define S_DEAD       0x20 /* link whose target is not here          */
#define STYLE_HEAD(s) (((s) & S_HEAD_MASK) >> 2)

/* Heading sizes: 0 = body text, 1 = 2x2, 2 = 1x2 (tall), 3 = 1x1 bold. */
static inline uint8_t head_sx(uint8_t h) { return h == 1 ? 2 : 1; }
static inline uint8_t head_sy(uint8_t h) { return (h == 1 || h == 2) ? 2 : 1; }

enum { LK_TEXT = 0, LK_RULE = 1, LK_IMAGE = 2 };

enum {
    DOC_OK = 0,
    DOC_BAD_HEADER, /* too short or inconsistent header */
    DOC_BAD_MAGIC,  /* not an html2ce page              */
    DOC_BAD_VERSION /* format version we don't know     */
};

typedef struct {
    char     title[41];
    char     prev[9];     /* AppVar name of previous page, "" if none */
    char     next[9];     /* AppVar name of next page, "" if none     */
    uint16_t nlines;
    uint16_t total_h;     /* total height of the page in pixels       */
    uint16_t arena_used;  /* bytes of ARENA_CAP this page needed      */
    bool     truncated;   /* page didn't fit the buffers              */
} Doc;

/* One laid-out line.
 *   LK_TEXT : p[0] = number of segments, then per segment
 *             [style][length][characters...]
 *   LK_RULE : no payload
 *   LK_IMAGE: p[0..7] sprite AppVar name, p[8] width, p[9] height          */
typedef struct {
    uint8_t        kind;
    uint8_t        center;  /* 1 = centre inside the text area */
    uint8_t        indent;  /* left indent in pixels           */
    uint8_t        height;  /* line height in pixels           */
    uint16_t       y;       /* top of the line within the page */
    const uint8_t *p;
} LineView;

/* Parse and lay out a page. `data` points at the AppVar contents (after the
 * 2-byte size). Replaces any previously laid-out page. */
int doc_layout(Doc *d, const uint8_t *data, uint16_t size);

bool     doc_line(const Doc *d, uint16_t i, LineView *v);
uint16_t doc_y(const Doc *d, uint16_t i);                 /* top of line i              */
uint16_t doc_line_at(const Doc *d, uint16_t y);           /* first line with top >= y   */
uint16_t doc_max_top(const Doc *d, uint16_t view_height); /* last useful first line     */

#endif
