/* KolibriOS clipboard <-> X11 CLIPBOARD selection bridge.
 *
 * kex stores the KolibriOS clipboard in shared memory (k_clipboard_* in
 * k_mem.c) where only other KolibriOS processes can see it. This module also
 * mirrors it onto the X CLIPBOARD selection in both directions:
 *
 *   copy in KolibriOS -> k_clipboard_add -> k_clip_publish -> we own CLIPBOARD
 *   copy on the host  -> k_clip_sync (from fn 54.0) -> new KolibriOS slot
 *
 * X selection traffic is only serviced while the guest is inside
 * k_process_event, which is where kex pumps the X queue; an app that never
 * calls fn 10/11/23 will not answer paste requests from host applications.
 */
#include "k_clip.h"
#include "k_mem.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

extern Display* display;                 /* opened in k_gui.c */

/* KolibriOS clipboard block header: size (this field included), data type,
 * text encoding, then the data. Only the size field is kernel-defined; the
 * rest is the userland convention every KolibriOS app follows. */
#define KCLIP_TYPE_TEXT      0
#define KCLIP_TYPE_TEXTBLOCK 1
#define KCLIP_ENC_UTF8       0
#define KCLIP_ENC_866        1
#define KCLIP_ENC_1251       2

/* Refuse anything larger than this in either direction. Selections above a
 * few hundred KB need the INCR protocol, which is not implemented. */
#define KCLIP_MAX (256*1024)

static const unsigned short cp866_hi[128] = {
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
    0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0,
};
static const unsigned short cp1251_hi[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

/* ---------------- encoding helpers ---------------- */
static size_t utf8_put(char* d, size_t pos, size_t cap, unsigned cp)
{
    if(cp < 0x80)          { if(pos+1>cap) return 0; d[pos++] = cp; }
    else if(cp < 0x800)    { if(pos+2>cap) return 0;
        d[pos++] = 0xC0|(cp>>6);  d[pos++] = 0x80|(cp&0x3F); }
    else if(cp < 0x10000)  { if(pos+3>cap) return 0;
        d[pos++] = 0xE0|(cp>>12); d[pos++] = 0x80|((cp>>6)&0x3F); d[pos++] = 0x80|(cp&0x3F); }
    else                   { if(pos+4>cap) return 0;
        d[pos++] = 0xF0|(cp>>18); d[pos++] = 0x80|((cp>>12)&0x3F);
        d[pos++] = 0x80|((cp>>6)&0x3F); d[pos++] = 0x80|(cp&0x3F); }
    return pos;
}

/* Single-byte code page -> UTF-8. Returns the number of bytes written. */
static size_t sbcs_to_utf8(const unsigned char* s, size_t n, char* d, size_t cap,
                           const unsigned short* hi)
{
    size_t out = 0;
    for(size_t i=0; i<n; ++i)
    {
        unsigned c = s[i];
        if(c == 0) break;
        size_t k = utf8_put(d, out, cap, c < 0x80 ? c : hi[c-0x80]);
        if(k == 0) break;
        out = k;
    }
    return out;
}

/* Decode one UTF-8 code point, advancing *i. Invalid input yields U+FFFD. */
static unsigned utf8_get(const unsigned char* s, size_t n, size_t* i)
{
    size_t p = *i;
    unsigned c = s[p++], cp, extra;
    if(c < 0x80)                 { *i = p; return c; }
    else if((c & 0xE0) == 0xC0)  { cp = c & 0x1F; extra = 1; }
    else if((c & 0xF0) == 0xE0)  { cp = c & 0x0F; extra = 2; }
    else if((c & 0xF8) == 0xF0)  { cp = c & 0x07; extra = 3; }
    else                         { *i = p; return 0xFFFD; }
    if(p + extra > n)            { *i = n; return 0xFFFD; }
    while(extra--)
    {
        unsigned cc = s[p++];
        if((cc & 0xC0) != 0x80) { *i = p; return 0xFFFD; }
        cp = (cp << 6) | (cc & 0x3F);
    }
    *i = p;
    return cp;
}

/* UTF-8 -> Latin-1, for the legacy STRING target. */
static size_t utf8_to_latin1(const unsigned char* s, size_t n, char* d, size_t cap)
{
    size_t out = 0, i = 0;
    while(i < n && out < cap)
    {
        unsigned cp = utf8_get(s, n, &i);
        d[out++] = cp < 0x100 ? (char)cp : '?';
    }
    return out;
}

/* ---------------- selection state ---------------- */
static Window clip_win;                      /* owns the selection */
static Atom A_CLIPBOARD, A_TARGETS, A_UTF8, A_TEXT, A_INCR, A_TIMESTAMP, A_PROP;
static char*  own_text;                      /* UTF-8 we are offering */
static size_t own_len;
static char*  seen_text;                     /* last text pulled in from X */
static size_t seen_len;
static int    warned_incr;

static int clip_init(void)
{
    if(display == NULL) return 0;
    if(clip_win) return 1;

    clip_win = XCreateSimpleWindow(display, RootWindow(display,0), -10, -10, 1, 1, 0, 0, 0);
    if(clip_win == 0) return 0;
    A_CLIPBOARD = XInternAtom(display, "CLIPBOARD",     False);
    A_TARGETS   = XInternAtom(display, "TARGETS",       False);
    A_UTF8      = XInternAtom(display, "UTF8_STRING",   False);
    A_TEXT      = XInternAtom(display, "TEXT",          False);
    A_INCR      = XInternAtom(display, "INCR",          False);
    A_TIMESTAMP = XInternAtom(display, "TIMESTAMP",     False);
    A_PROP      = XInternAtom(display, "KEX_SELECTION", False);
    return 1;
}

/* ---------------- KolibriOS -> X11 ---------------- */
void k_clip_publish(const void* buf, unsigned size)
{
    const unsigned char* b = (const unsigned char*)buf;
    if(!clip_init() || size < 12 || size > KCLIP_MAX) return;

    DWORD type = b[4] | (b[5]<<8) | (b[6]<<16) | ((DWORD)b[7]<<24);
    DWORD enc  = b[8] | (b[9]<<8) | (b[10]<<16) | ((DWORD)b[11]<<24);
    if(type != KCLIP_TYPE_TEXT && type != KCLIP_TYPE_TEXTBLOCK) return;

    const unsigned char* data = b + 12;
    size_t dlen = size - 12;
    size_t cap = dlen*3 + 4, out;
    char* utf8 = (char*)malloc(cap);
    if(utf8 == NULL) return;

    if(enc == KCLIP_ENC_866)       out = sbcs_to_utf8(data, dlen, utf8, cap, cp866_hi);
    else if(enc == KCLIP_ENC_1251) out = sbcs_to_utf8(data, dlen, utf8, cap, cp1251_hi);
    else                           /* already UTF-8 */
    {
        out = 0;
        while(out < dlen && data[out]) { utf8[out] = data[out]; ++out; }
    }

    free(own_text);
    own_text = utf8;
    own_len  = out;
    /* Remember it as "seen" too, so the very text we just published is not
     * pulled straight back in as though the host had copied it. */
    free(seen_text);
    seen_text = (char*)malloc(out ? out : 1);
    if(seen_text) { memcpy(seen_text, utf8, out); seen_len = out; } else seen_len = 0;

    XSetSelectionOwner(display, A_CLIPBOARD, clip_win, CurrentTime);
    XFlush(display);
}

/* Answer a paste request from a host application. */
static void serve_request(XSelectionRequestEvent* rq)
{
    XSelectionEvent rsp;
    memset(&rsp, 0, sizeof rsp);
    rsp.type      = SelectionNotify;
    rsp.display   = rq->display;
    rsp.requestor = rq->requestor;
    rsp.selection = rq->selection;
    rsp.target    = rq->target;
    rsp.time      = rq->time;
    rsp.property  = None;                       /* refused unless set below */

    Atom prop = rq->property != None ? rq->property : rq->target;

    if(rq->target == A_TARGETS)
    {
        Atom list[] = { A_TARGETS, A_TIMESTAMP, A_UTF8, A_TEXT, XA_STRING };
        XChangeProperty(display, rq->requestor, prop, XA_ATOM, 32, PropModeReplace,
                        (unsigned char*)list, sizeof list / sizeof list[0]);
        rsp.property = prop;
    }
    else if(rq->target == A_TIMESTAMP)
    {
        Time t = CurrentTime;
        XChangeProperty(display, rq->requestor, prop, XA_INTEGER, 32, PropModeReplace,
                        (unsigned char*)&t, 1);
        rsp.property = prop;
    }
    else if(own_text && (rq->target == A_UTF8 || rq->target == A_TEXT))
    {
        XChangeProperty(display, rq->requestor, prop, A_UTF8, 8, PropModeReplace,
                        (unsigned char*)own_text, (int)own_len);
        rsp.property = prop;
    }
    else if(own_text && rq->target == XA_STRING)
    {
        char* l1 = (char*)malloc(own_len ? own_len : 1);
        if(l1)
        {
            size_t n = utf8_to_latin1((unsigned char*)own_text, own_len, l1, own_len);
            XChangeProperty(display, rq->requestor, prop, XA_STRING, 8, PropModeReplace,
                            (unsigned char*)l1, (int)n);
            rsp.property = prop;
            free(l1);
        }
    }

    XSendEvent(display, rq->requestor, False, 0, (XEvent*)&rsp);
    XFlush(display);
}

int k_clip_event(void* xevent)
{
    XEvent* ev = (XEvent*)xevent;
    if(clip_win == 0) return 0;

    if(ev->type == SelectionRequest && ev->xselectionrequest.owner == clip_win)
    {
        serve_request(&ev->xselectionrequest);
        return 1;
    }
    if(ev->type == SelectionClear && ev->xselectionclear.window == clip_win)
    {
        free(own_text); own_text = NULL; own_len = 0;
        return 1;
    }
    return 0;
}

/* ---------------- X11 -> KolibriOS ---------------- */
/* Ask the selection owner for one target and read the reply. Returns a
 * malloc'd buffer, or NULL. */
static char* fetch_target(Atom target, size_t* out_len)
{
    XDeleteProperty(display, clip_win, A_PROP);
    XConvertSelection(display, A_CLIPBOARD, target, A_PROP, clip_win, CurrentTime);
    XFlush(display);

    /* Bounded wait. Non-matching events stay queued for k_process_event. */
    XEvent ev;
    int waited = 0;
    while(!XCheckTypedWindowEvent(display, clip_win, SelectionNotify, &ev))
    {
        if(++waited > 250) return NULL;                 /* ~250 ms */
        usleep(1000);
    }
    if(ev.xselection.property == None) return NULL;

    Atom type; int fmt; unsigned long nitems, after; unsigned char* data = NULL;
    if(XGetWindowProperty(display, clip_win, A_PROP, 0, KCLIP_MAX/4, True, AnyPropertyType,
                          &type, &fmt, &nitems, &after, &data) != Success)
        return NULL;
    if(data == NULL) return NULL;

    if(type == A_INCR)
    {
        /* Large transfers use the incremental protocol, which we do not
         * implement; better to paste nothing than half of it. */
        if(!warned_incr)
        {
            warned_incr = 1;
            fprintf(stderr, "kex: clipboard too large to paste (INCR not supported)\n");
        }
        XFree(data);
        return NULL;
    }
    if(fmt != 8) { XFree(data); return NULL; }

    char* copy = (char*)malloc(nitems ? nitems : 1);
    if(copy) { memcpy(copy, data, nitems); *out_len = nitems; }
    XFree(data);
    return copy;
}

void k_clip_sync(void)
{
    if(!clip_init()) return;

    /* fn 54.0 is a polling call - RDPclient alone asks twice a second - and
     * each sync is a selection round trip. Rate-limit so an app that spins on
     * the slot count cannot saturate the X connection. */
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long long ms = (long long)now.tv_sec*1000 + now.tv_nsec/1000000;
    static long long last_ms;
    if(last_ms && ms - last_ms < 250) return;
    last_ms = ms;

    Window owner = XGetSelectionOwner(display, A_CLIPBOARD);
    if(owner == None || owner == clip_win) return;     /* nothing, or our own */

    size_t len = 0;
    char* text = fetch_target(A_UTF8, &len);
    if(text == NULL) text = fetch_target(XA_STRING, &len);   /* legacy owner */
    if(text == NULL) return;

    /* Trailing NULs are common; drop them so the comparison is stable. */
    while(len && text[len-1] == 0) --len;
    if(len == 0 || len > KCLIP_MAX) { free(text); return; }

    if(seen_text && seen_len == len && memcmp(seen_text, text, len) == 0)
    {
        free(text);                                    /* already imported */
        return;
    }
    free(seen_text);
    seen_text = text;
    seen_len  = len;

    /* Publish as a KolibriOS slot: header + UTF-8 payload. */
    DWORD total = (DWORD)(12 + len);
    unsigned char* blk = (unsigned char*)malloc(total);
    if(blk == NULL) return;
    blk[0] = total; blk[1] = total>>8; blk[2] = total>>16; blk[3] = total>>24;
    memset(blk+4, 0, 8);                   /* type = text, encoding = UTF-8 */
    memcpy(blk+12, text, len);
    k_clipboard_add_host(total, blk);
    free(blk);
}
