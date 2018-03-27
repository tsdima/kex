#include "k_mem.h"
#include "k_event.h"
#include "k_iconv.h"
#include "k_ipc.h"
#include "k_gui.h"

#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/extensions/Xrender.h>

#define window_style ((ctx->window_color>>24)&15)

Display* display; Window win; Picture picture;
GlyphSet gsfont[2][8];

K_SKIN_PARAMS skin_params;
K_SKIN_BUTTON skin_button_minimize;
K_SKIN_BUTTON skin_button_close;

typedef struct
{
    DWORD width;
    DWORD height;
    XImage* img;
} kpixmap;

kpixmap kpixmaps[2][3];

void SetNoBorder()
{
    int set = 0;
    Atom WM_HINTS;

    /* First try to set MWM hints */
    WM_HINTS = XInternAtom(display, "_MOTIF_WM_HINTS", True);
    if(WM_HINTS != None)
    {
        struct {
            unsigned long flags;
            unsigned long functions;
            unsigned long decorations;
            long input_mode;
            unsigned long status;
        } MWMHints = { (1L << 1), 0, 0, 0, 0 };

        XChangeProperty(display, win, WM_HINTS, WM_HINTS, 32,
            PropModeReplace, (unsigned char *)&MWMHints,
            sizeof(MWMHints)/sizeof(long));
        set = 1;
    }

    /* Now try to set KWM hints */
    WM_HINTS = XInternAtom(display, "KWM_WIN_DECORATION", True);
    if(WM_HINTS != None)
    {
        long KWMHints = 0;
        XChangeProperty(display, win, WM_HINTS, WM_HINTS, 32,
            PropModeReplace, (unsigned char *)&KWMHints,
            sizeof(KWMHints)/sizeof(long));
        set = 1;
    }

    /* Now try to set GNOME hints */
    WM_HINTS = XInternAtom(display, "_WIN_HINTS", True);
    if(WM_HINTS != None)
    {
        long GNOMEHints = 0;
        XChangeProperty(display, win, WM_HINTS, WM_HINTS, 32,
            PropModeReplace, (unsigned char *)&GNOMEHints,
            sizeof(GNOMEHints)/sizeof(long));
        set = 1;
    }

    /* Finally set the transient hints if necessary */
    if(!set) XSetTransientForHint(display, win, RootWindow(display, 0));
}

void k_gui_init()
{
    display = XOpenDisplay(NULL);
    if (DefaultDepth(display, 0) < 24) k_panic("Cannot work in non true color mode");
}

void k_pixmap_setimage(kpixmap* pm, BYTE* image, int width, int height)
{
    DWORD* image32 = (DWORD*)malloc(width*height*4),idx;

    for(idx = 0; idx < width * height; ++idx)
        image32[idx] = 0xffffff & *(DWORD*)(image + idx * 3);

    pm->width = width;
    pm->height = height;
    if(pm->img) XDestroyImage(pm->img);
    pm->img = XCreateImage(display, DefaultVisual(display, 0), 24, ZPixmap, 0, (char*)image32, width, height, 32, 0);
}

void k_set_skin(BYTE* skin)
{
    K_SKIN_HDR* header = (K_SKIN_HDR*)skin;
    if (header==NULL || header->magic != 0x4e494b53 || header->version!=1) k_panic("Skin loading error");

    K_SKIN_PARAMS* params = (K_SKIN_PARAMS*)(skin+header->params);
    K_SKIN_BUTTON* button = (K_SKIN_BUTTON*)(skin+header->buttons);
    K_SKIN_BMPDEF* bitmap = (K_SKIN_BMPDEF*)(skin+header->bitmaps);

    skin_params = *params;

    for(; button->type!=0; ++button)
    {
        switch(button->type)
        {
        case 1: skin_button_close = *button; break;
        case 2: skin_button_minimize = *button; break;
        }
    }

    for(; bitmap->kind!=0; ++bitmap)
    {
        if (bitmap->kind<=3 && bitmap->type<=1)
        {
            K_SKIN_BITMAP* bmp = (K_SKIN_BITMAP*)(skin+bitmap->offset);
            k_pixmap_setimage(&kpixmaps[bitmap->type][bitmap->kind-1], bmp->data, bmp->width, bmp->height);
        }
    }
}

void k_define_skin_button(k_context* ctx, K_SKIN_BUTTON* b, DWORD width, DWORD height)
{
    int x = b->x, y = b->y;
    if (x<0) x += width;
    if (y<0) y += height;
    k_define_button(ctx, x-1, y-1, b->width+1, b->height+1, b->type==1 ? 1 : 0xFFFF);
}

DWORD k_get_skin_height() { return skin_params.height; }

void k_get_skin_colors(BYTE* buf, DWORD size)
{
    if (size>skin_params.dtpfsize) size = skin_params.dtpfsize;
    memcpy(buf, skin_params.dtp, size);
}

void k_get_client_rect(k_context* ctx, DWORD* x, DWORD* y, DWORD* w, DWORD* h)
{
    *x = 5;
    *y = skin_params.height;
    *w = ctx->window_w-1-2*5;
    *h = ctx->window_h-1-5-skin_params.height;
}

void k_get_screen_size(DWORD* width, DWORD* height)
{
    *width = 1920; //DisplayWidth(display,0);
    *height = 1080; //DisplayHeight(display,0);
}

void k_get_desktop_rect(DWORD* left, DWORD* top, DWORD* right, DWORD* bottom)
{
    KERNEL_MEM* km = kernel_mem();
    *left = km->desktop_left;
    *top = km->desktop_top;
    *right = km->desktop_right;
    *bottom = km->desktop_bottom;
}

void k_set_desktop_rect(WORD left, WORD top, WORD right, WORD bottom)
{
    KERNEL_MEM* km = kernel_mem(); DWORD sw,sh;
    k_get_screen_size(&sw, &sh);
    if(left&0x8000) left = km->desktop_left;
    if(top&0x8000) top = km->desktop_top;
    if(left<right)
    {
        km->desktop_left = left;
        if(right<sw) km->desktop_right = right;
    }
    if(top<bottom)
    {
        km->desktop_top = top;
        if(bottom<sw) km->desktop_bottom = bottom;
    }
}

void k_move_mouse(int x, int y)
{
    XWarpPointer(display, None, RootWindow(display, 0), 0,0,0,0, x,y);
}

void k_frame(Window win, GC gc, int x1, int y1, int x2, int y2, int width)
{
    XFillRectangle(display, win, gc, x1, y1, width, y2-y1);
    XFillRectangle(display, win, gc, x1, y2-width, x2-x1, width);
    XFillRectangle(display, win, gc, x2-width, y1, width, y2-y1);
}

void k_draw_button_pressed(k_button* b)
{
    XGCValues val;
    val.function = GXxor;
    val.foreground = 0xFFFFFF;
    GC gc = XCreateGC(display, win, GCFunction|GCForeground, &val);
    XFillRectangle(display, win, gc, b->x+1, b->y+1, b->w-1, 1);
    XFillRectangle(display, win, gc, b->x+1, b->y+2, 1, b->h-3);
    XFillRectangle(display, win, gc, b->x+1, b->y+b->h-1, b->w-1, 1);
    XFillRectangle(display, win, gc, b->x+b->w-1, b->y+2, 1, b->h-3);
    XFreeGC(display, gc);
}

XRenderColor xr_convert_color(DWORD color)
{
    XRenderColor xrc;
    xrc.blue = ((color<<8)&0xFF00)|((color)&0xFF);
    xrc.green = ((color)&0xFF00)|((color>>8)&0xFF);
    xrc.red = ((color>>8)&0xFF00)|((color>>16)&0xFF);
    xrc.alpha = 0xFFFF;
    return xrc;
}

Picture xr_create_picture(int width, int height, int repeat)
{
    Pixmap pm = XCreatePixmap(display, RootWindow(display, 0), width, height, 32);
    XRenderPictureAttributes attr; attr.repeat = repeat;
    Picture p = XRenderCreatePicture(display, pm, XRenderFindStandardFormat(display, PictStandardARGB32), CPRepeat, &attr);
    XFreePixmap(display, pm);
    return p;
}

Picture xr_create_pen(DWORD color)
{
    XRenderColor xrc = xr_convert_color(color); Picture p = xr_create_picture(1,1,1);
    XRenderFillRectangle(display, PictOpOver, p, &xrc, 0, 0, 1, 1);
    return p;
}

void k_pixel8(BYTE* p, int mode, DWORD color)
{
    *p = mode==0 ? 0xFF : 0x3F;
}

void k_pixel32(BYTE* p, int mode, DWORD color)
{
    if(mode==0)
    {
        *(DWORD*)p = color;
    }
    else
    {
        *p = (*p>>2)*3+((color>>2)&0x3F); ++p;
        *p = (*p>>2)*3+((color>>10)&0x3F); ++p;
        *p = (*p>>2)*3+((color>>18)&0x3F);
    }
}

void k_prepare_char(BYTE* ch, int width, int height, BYTE* buf, DWORD stride, DWORD s, void(*putpix)(BYTE*,int,DWORD), DWORD color, DWORD pixsz)
{
    DWORD x,y,w,h,b0,b1,b2; BYTE map[16][8],*d; memset(*map,0,sizeof(map));
    for(y=0; y<height; ++y,++ch)
    {
        b0 = y==0?0:ch[-1]<<1; b1 = ch[0]<<5; b2 = ch[1]<<9;
        for(x=0; x<width; ++x,b0>>=1,b1>>=1,b2>>=1)
        {
            w = (b2&0x700)|(b1&0x70)|(b0&7);
            if((w&0x670)==0x240 || (w&0x770)==0x350 || w==0x640) map[y][x] |= 1;
            if((w&0x370)==0x210 || (w&0x770)==0x650 || w==0x310) map[y][x] |= 2;
            if((w&0x076)==0x042 || (w&0x077)==0x053 || w==0x046) map[y][x] |= 4;
            if((w&0x073)==0x012 || (w&0x077)==0x056 || w==0x013) map[y][x] |= 8;
            if((w&0x770)==0x250) map[y][x] |= 3;
            if((w&0x077)==0x052) map[y][x] |= 12;
            if(w&0x20) map[y][x] = 0x10;
        }
    }
    if(s==1)
    {
        for(y=0; y<height; ++y) for(x=0,d=buf+y*stride; x<width; ++x,d+=pixsz)
        {
            char m = map[y][x];
            if((m&0x10)!=0) putpix(d,0,color); else
            if((m&0x0F)!=0) putpix(d,1,color);
        }
    }
    else
    {
        for(y=0; y<height; ++y) for(x=0; x<width; ++x)
        {
            char m = map[y][x];
            for(h=0; h<s; ++h) for(w=0,d=buf+(y*s+h)*stride+x*s*pixsz; w<s; ++w,d+=pixsz)
            {
                if(((m&1)!=0 && w>s-h-1) ||
                    ((m&2)!=0 && w<h) ||
                    ((m&4)!=0 && w>h) ||
                    ((m&8)!=0 && w<s-h-1) ||
                    ((m&0x10)!=0)) putpix(d,0,color);
            }
        }
    }
}

GlyphSet xr_get_font(k_bitmap_font* bf, int scale)
{
    int size = bf->height==9 ? 0 : 1;
    if(gsfont[size][scale]==0)
    {
        Glyph i; BYTE* ch; DWORD s=scale+1,stride = ((bf->width*s-1)|3)+1;
        XGlyphInfo info = {.width = bf->width*s, .height = bf->height*s, .x = 0, .y = 0, .xOff = bf->width*s, .yOff = 0};
        GlyphSet font = XRenderCreateGlyphSet(display, XRenderFindStandardFormat(display, PictStandardA8));
        for(i=0,ch=bf->bmp; i<bf->chars; ++i,ch+=bf->height)
        {
            static BYTE buf[16*8*8*8]; memset(buf,0,bf->height*s*stride);
            k_prepare_char(ch, bf->width, bf->height, buf, stride, s, k_pixel8, 0, 1);
            XRenderAddGlyphs(display, font, &i, &info, 1, (char*)buf, bf->height*s*stride);
        }
        gsfont[size][scale] = font;
    }
    return gsfont[size][scale];
}

void k_draw_text_intern(k_context* ctx, int x, int y, BYTE* text, int len, int fillbg, int cp, int buf, int scale, DWORD color, DWORD extra)
{
    if(text==NULL) return; k_bitmap_font* bf = cp==0 ? &font9 : &font16;
    if(len<0||len>512) len = k_strlen(text, cp);
    if(buf)
    {
        x -= ctx->client_x; y -= ctx->client_y;
        DWORD* data = user_pd(extra), width=*data++, height=*data++;
        int maxlen = (width-x)/bf->width,i;
        if(len>maxlen) len = maxlen; if(len<=0) return;
        if(y+bf->height>height) return;
        if(cp==0)
        {
            for(i=0; i<len; ++i)
                k_prepare_char(bf->bmp+text[i]*bf->height, bf->width, bf->height,
                    (BYTE*)(data+y*width+x+i*bf->width*(scale+1)), width*4, scale+1, k_pixel32, color, 4);
        }
        else
        {
            WORD wbuf[512]; if(len>511) len=511; k_strncpy((BYTE*)wbuf, 2, text, cp, len);
            for(i=0; i<len; ++i)
                k_prepare_char(bf->bmp+wbuf[i]*bf->height, bf->width, bf->height,
                    (BYTE*)(data+y*width+x+i*bf->width*(scale+1)), width*4, scale+1, k_pixel32, color, 4);
        }
    }
    else
    {
        int maxlen = (ctx->window_w-x)/bf->width;
        if(len>maxlen) len = maxlen; if(len<=0) return;
        int sx = len*(scale+1)*bf->width, sy = (scale+1)*bf->height;
        if(fillbg)
        {
            XRenderColor bgcolor = xr_convert_color(extra);
            XRenderFillRectangle(display, PictOpSrc, picture, &bgcolor, x,y, sx,sy);
        }
        Picture fg_pen = xr_create_pen(color);
        if(cp==0)
        {
            XRenderCompositeString8(display, PictOpOver, fg_pen, picture, NULL, xr_get_font(bf,scale), 0,0, x,y, (char*)text, len);
        }
        else
        {
            WORD wbuf[512]; if(len>511) len=511; k_strncpy((BYTE*)wbuf, 2, text, cp, len);
            XRenderCompositeString16(display, PictOpOver, fg_pen, picture, NULL, xr_get_font(bf,scale), 0,0, x,y, wbuf, len);
        }
        XRenderFreePicture(display, fg_pen);
    }
}

void k_fill_rectangle(k_context* ctx, int x, int y, int width, int height, DWORD color)
{
    if((color&0x80000000)==0)
    {
        XGCValues val; val.foreground = color&0xFFFFFF;
        GC gc = XCreateGC(display, win, GCForeground, &val);
        XFillRectangle(display, win, gc, x, y, width, height);
        XFreeGC(display, gc);
    }
    else
    {
        XLinearGradient lg = {{0,0},{0,XDoubleToFixed(ctx->window_h/2)}}; XFixed stops[] = {0, XDoubleToFixed(1.0f)};
        XRenderColor cols[] = {xr_convert_color(color&0xFFFFFF), xr_convert_color(0)};
        Picture p = XRenderCreateLinearGradient(display, &lg, stops, cols, 2);
        XRenderComposite(display, PictOpSrc, p, 0, picture, 0,0, 0,0, x,y, width,height);
        XRenderFreePicture(display, p);
    }
}

void k_draw_window_intern(k_context* ctx, int onlyframe)
{
    if(window_style<=1 && (ctx->window_color&0x40000000)==0)
    {
        if(onlyframe==0) k_fill_rectangle(ctx, 0, 0, ctx->window_w, ctx->window_h, ctx->window_color);
        return;
    }
    if(window_style!=3 && window_style!=4) return;

    int ofs; DWORD width = ctx->window_w, height = ctx->window_h;

    // Title bar
    kpixmap* left = kpixmaps[ctx->focused&1];
    kpixmap* oper = left+1;
    kpixmap* base = left+2;
    XPutImage(display, win, DefaultGC(display, 0), left->img, 0,0, 0,0, left->width,left->height);
    if (width > oper->width)
    {
        for(ofs = left->width; ofs < width - oper->width; ofs += base->width)
        {
            XPutImage(display, win, DefaultGC(display, 0), base->img, 0,0, ofs,0, base->width,base->height);
        }
    }
    XPutImage(display, win, DefaultGC(display, 0), oper->img, 0,0, width - oper->width,0, oper->width,oper->height);

    K_SKIN_COLORS* col = ctx->focused ? &skin_params.acolor : &skin_params.icolor;

    // Border
    XGCValues val; val.foreground = col->outer;
    GC gc = XCreateGC(display, win, GCForeground, &val);
    k_frame(win, gc, 0, skin_params.height, width, height, 1);
    XSetForeground(display, gc, col->frame);
    k_frame(win, gc, 1, skin_params.height, width-1, height-1, 3);
    XSetForeground(display, gc, col->inner);
    k_frame(win, gc, 4, skin_params.height, width-4, height-4, 1);

    // Caption
    if(ctx->window_title!=NULL)
    {
        k_draw_text_intern(ctx, skin_params.lmargin, (skin_params.height-14)/2, (BYTE*)ctx->window_title, -1, 0, 3, 0, 0,
            skin_params.dtpfsize<5*sizeof(DWORD)?0:skin_params.dtp[4], 0);
    }

    // Client area
    if((ctx->window_color&0x40000000)==0 && onlyframe==0)
    {
        k_fill_rectangle(ctx, skin_params.lmargin, skin_params.height,
            width-2*skin_params.lmargin, height-skin_params.height-skin_params.lmargin, ctx->window_color);
    }

    XFreeGC(display, gc);

    k_remove_button(ctx, 1); k_remove_button(ctx, 0xFFFF);
    k_define_skin_button(ctx, &skin_button_close, width, height);
    k_define_skin_button(ctx, &skin_button_minimize, width, height);

    if(ctx->button_id_pressed!=0 && (ctx->button_id_pressed&KBS_NO_PRESS)==0)
    {
        k_button* b = k_find_button_by_id(ctx, ctx->button_id_pressed);
        if (b!=NULL) k_draw_button_pressed(b);
    }
}

void k_move_window(k_context* ctx, int x, int y)
{
    if (win==0) return;
    if(x!=ctx->window_x || y!=ctx->window_y)
        XMoveWindow(display, win, x, y);
    ctx->window_x = x;
    ctx->window_y = y;
}

void k_fix_size(k_context* ctx, int width, int height)
{
    XSizeHints size;
    size.flags = PMinSize | PMaxSize;
    size.max_width = 0;
    size.min_width = width;
    size.max_height = 0;
    size.min_height = height;
    XSetWMNormalHints(display, win, &size);
}

void k_move_size_window(k_context* ctx, int x, int y, int width, int height)
{
    if (win==0) return;
    if (x==-1) x = ctx->window_x;
    if (y==-1) y = ctx->window_y;
    if (width==-1) width = ctx->window_w-1;
    if (height==-1) height = ctx->window_h-1;
    ++width; ++height;
    if (window_style<=1 || window_style==4) k_fix_size(ctx, width, height);
    if (width!=ctx->window_w || height!=ctx->window_h)
        XMoveResizeWindow(display, win, x, y, width, height);
    else if(x!=ctx->window_x || y!=ctx->window_y)
        XMoveWindow(display, win, x, y);
    ctx->window_x = x;
    ctx->window_y = y;
    ctx->window_w = width;
    ctx->window_h = height;
    k_draw_window_intern(ctx,0);
    k_event_redraw(ctx);
    usleep(2000); // hack
}


void k_wm_state(k_context* ctx, char *atom, int state)
{
    if (win==0) return;
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = win;
    ev.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", 0);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = state&1;
    ev.xclient.data.l[1] = XInternAtom(display, atom, 0);
    ev.xclient.data.l[2] = 0;
    XSendEvent(display, RootWindow(display, 0), 0, SubstructureNotifyMask, &ev);
}

void k_raise_window(k_context* ctx)
{
    if (win==0) return;
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = win;
    ev.xclient.message_type = XInternAtom(display, "_NET_ACTIVE_WINDOW", 0);
    ev.xclient.format = 32;
    XSendEvent(display, RootWindow(display, 0), 0, SubstructureRedirectMask|SubstructureNotifyMask, &ev);
    XMapRaised(display, win);
}

DWORD k_get_pixel(k_context* ctx, int x, int y)
{
    XImage* xim = XGetImage(display, RootWindow(display, 0), x, y, 1, 1, AllPlanes, XYPixmap);
    unsigned long xpixel = XGetPixel(xim, 0, 0); XFree(xim);
    return xpixel&0xFFFFFF;
}

void k_get_image(k_context* ctx, int x, int y, DWORD width, DWORD height, BYTE* img)
{
    unsigned long w = RootWindow(display, 0);
    if(win!=0 && x>=ctx->window_x && y>=ctx->window_y && x+width+1<ctx->window_x+ctx->window_w && y+height+1<ctx->window_y+ctx->window_h)
    {
        XWindowAttributes wa; XGetWindowAttributes(display, win, &wa);
        if(wa.map_state==IsViewable) { w = win; x -= ctx->window_x; y -= ctx->window_y; }
    }
    XImage* xim = XGetImage(display, w, x, y, width, height, AllPlanes, XYPixmap);
    for(y = 0; y < height; ++y) for(x = 0; x < width; ++x)
    {
        unsigned long xpixel = XGetPixel(xim, x, y);
        *img++ = xpixel; xpixel>>=8; *img++ = xpixel; xpixel>>=8; *img++ = xpixel;
    }
    XFree(xim);
}

static BYTE xscan2kex[256] = {
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,

    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x47,0x4B,0x48,0x4D, 0x50,0x49,0x51,0x4F, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x52, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0xC5,

    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x1C,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x37,0x00, 0x53,0x4A,0x00,0x35,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,

    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x53
};

static WORD xchar2kex[256] = {
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,

    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x1B4,0x1B0,0x1B2,0x1B3, 0x1B1,0x1B8,0x1B7,0x1B5, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x1B9, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x02,

    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x0D,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0xB4,0xB0,0xB2, 0xB3,0xB1,0xB8,0xB7, 0xB5,0x37,0xB9,0xB6,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x2A,0x2B, 0x2C,0x2D,0x00,0x2F,
    0x30,0x31,0x32,0x33, 0x34,0x35,0x36,0x37, 0x38,0x39,0x00,0x00, 0x00,0x00,0x00,0x00,

    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x1B6
};

DWORD k_translate_xkey(XKeyEvent* e, int* ext, int* mod)
{
    static KeySym modxlat[] = {XK_Shift_L, XK_Shift_R, XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R,
        XK_Caps_Lock, XK_Num_Lock, XK_Scroll_Lock, XK_Super_L, XK_Super_R}; DWORD i,m;
    KeySym key; XLookupString(e, NULL, 0, &key, NULL);
    BYTE scancode = e->keycode - 8;
    BYTE charcode = key; *ext = *mod = 0;
    if ((key>>8)==0xFF)
    {
        if (xscan2kex[charcode]!=0) scancode = xscan2kex[charcode];
        if (xchar2kex[charcode]!=0) { WORD extcode = xchar2kex[charcode]; charcode = extcode; *ext = extcode>>8; }
    }
    for(i=0,m=1; i<11; ++i,m<<=1) if(key==modxlat[i]) { *mod = m; break; }
    return (scancode<<16)|(charcode<<8);
}

void k_process_event(k_context* ctx)
{
    static DWORD btnxlat[] = {0,1,4,2,8,16}; DWORD button_id;
    static int x, y;

    if(XPending(display) == 0)
    {
        int xfd = ConnectionNumber(display),fdcnt = (xfd>ipc_server ? xfd : ipc_server)+1;
        fd_set fds; msg_t msg; struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 1000;
        for(;;)
        {
            FD_ZERO(&fds); FD_SET(xfd, &fds); FD_SET(ipc_server, &fds);
            if(select(fdcnt, &fds, NULL, NULL, &tv)<=0) return;
            if(FD_ISSET(ipc_server, &fds)) k_process_ipc_event(ctx, &msg); else break;
        }
    }

    while(XPending(display) != 0)
    {
        XEvent ev; XNextEvent(display, &ev);

        switch(ev.type)
        {
        case Expose:
            k_draw_window_intern(ctx,1);
            k_event_redraw(ctx);
            break;

        case KeyPress:
            button_id = k_translate_xkey(&ev.xkey, &x, &y);
            if(ctx->kbd_mode==1)
            {
                button_id = (button_id>>8)&0xFF00;
                if(x) k_event_keypress(ctx, 0xE000);
            }
            if(ctx->kbd_mode==1 || y==0) k_event_keypress(ctx, button_id);
            if(y&KMOD_CAPSLOCK) k_set_keyboard_modifiers(KMOD_CAPSLOCK, (ev.xkey.state&LockMask)!=0);
            if(y&KMOD_NUMLOCK) k_set_keyboard_modifiers(KMOD_NUMLOCK, (ev.xkey.state&Mod2Mask)!=0);
            if(y&KMOD_SCROLL) k_set_keyboard_modifiers(KMOD_SCROLL, (k_get_keyboard_modifiers()&KMOD_SCROLL)!=0);
            if(y&~KMOD_LOCKMASK) k_set_keyboard_modifiers(y,0);
            break;

        case KeyRelease:
            button_id = k_translate_xkey(&ev.xkey, &x, &y);
            if(ctx->kbd_mode==1)
            {
                button_id = (button_id>>8)&0xFF00;
                if(x) k_event_keypress(ctx, 0xE000);
                k_event_keypress(ctx, button_id|0x8000);
            }
            if(y&~KMOD_LOCKMASK) k_set_keyboard_modifiers(y,1);
            break;

        case ButtonPress:
            x = ev.xbutton.x;
            y = ev.xbutton.y;
            button_id = ctx->button_id_pressed;
            k_event_mousepress(ctx, btnxlat[ev.xbutton.button]);
            if(button_id != ctx->button_id_pressed && (ctx->button_id_pressed&KBS_NO_PRESS)==0)
            {
                k_button* b = k_find_button_by_id(ctx, ctx->button_id_pressed);
                if (b!=NULL) k_draw_button_pressed(b);
            }
            break;

        case ButtonRelease:
            button_id = ctx->button_id_pressed;
            k_event_mouserelease(ctx, btnxlat[ev.xbutton.button]);
            if(button_id != ctx->button_id_pressed && (button_id&KBS_NO_PRESS)==0)
            {
                k_button* b = k_find_button_by_id(ctx, button_id);
                if (b!=NULL) k_draw_button_pressed(b);
            }
            if(button_id==0xFFFF)
            {
                k_button* b = k_find_button(ctx);
                if (b!=NULL && b->id == 0xFFFF) XIconifyWindow(display, win, 0);
            }
            break;

        case MotionNotify:
            if((ev.xmotion.state & Button1Mask)!=0 && y<skin_params.height && ctx->button_id_pressed==0)
            {
                k_move_window(ctx, ev.xmotion.x_root - x, ev.xmotion.y_root - y);
            }
            k_event_mousemove(ctx, ev.xmotion.x_root, ev.xmotion.y_root);
            break;

        case FocusIn:
            if (ctx->focused==1) break;
            k_focus_in(ctx);
            ctx->focused = 1;
            k_draw_window_intern(ctx,1);
            k_event_redraw(ctx);
            break;

        case FocusOut:
            if (ctx->focused==0) break;
            ctx->focused = 0;
            k_draw_window_intern(ctx,1);
            k_event_redraw(ctx);
            break;

        case ConfigureNotify:
            if (ev.xconfigure.window == win && (ev.xconfigure.width>1 || ev.xconfigure.height>1))
            {
                ctx->window_x = ev.xconfigure.x;
                ctx->window_y = ev.xconfigure.y;
                ctx->window_w = ev.xconfigure.width;
                ctx->window_h = ev.xconfigure.height;
                ctx->mouse_x = kernel_mem()->mouse_x - ctx->window_x;
                ctx->mouse_y = kernel_mem()->mouse_y - ctx->window_y;
            }
            break;
        }
    }
}

void k_window(k_context* ctx, int x, int y, DWORD width, DWORD height, DWORD color, DWORD titleaddr)
{
    ctx->window_color = color;
    ctx->client_x = (color&0x20000000)==0 || window_style<3 ? 0 : 5;
    ctx->client_y = (color&0x20000000)==0 || window_style<3 ? 0 : skin_params.height;

    if (win==0)
    {
        ctx->window_x = 0; ctx->window_y = 0; ctx->window_w = 1; ctx->window_h = 1;

        if((window_style==3 || window_style==4) && (color&0x10000000)!=0 && titleaddr!=0)
        {
            ctx->window_title = (char*)k_strdup(user_pb(titleaddr),-1,3);
        }

        win = XCreateSimpleWindow(display, RootWindow(display, 0), 0, 0, 1, 1, 0, 0, color&0xFFFFFF); SetNoBorder();
        if(ctx->window_title)
            Xutf8SetWMProperties(display, win, ctx->window_title, NULL, NULL, 0, NULL, NULL, NULL);
        XMapWindow(display, win);
        XGrabKey(display, AnyKey, AnyModifier, win, True, GrabModeAsync, GrabModeAsync);
        XSelectInput(display, win, ExposureMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask|StructureNotifyMask|FocusChangeMask);
        picture = XRenderCreatePicture(display, win, XRenderFindStandardFormat(display, PictStandardRGB24), 0, 0);

        if(window_style<=1)
        {
            k_wm_state(ctx, "_NET_WM_STATE_SKIP_TASKBAR", 1);
            k_wm_state(ctx, "_NET_WM_STATE_SKIP_PAGER", 1);
        }

        if(window_style!=1)
        {
            DWORD sx,sy,sw,sh;
            k_get_desktop_rect(&sx, &sy, &sw, &sh);
            if(x<sx) x = sx; if(y<sy) y = sy;
            if(x+width>=sw) x = sw-width-1;
            if(y+height>=sh) y = sh-height-1;
        }

        k_move_size_window(ctx, x, y, width, height);
    }
    else
    {
        if(window_style==3 || window_style==4)
        {
            if((color&0x10000000)!=0 && titleaddr!=0)
            {
                char* title = (char*)k_strdup(user_pb(titleaddr),-1,3);
                if(strcmp(ctx->window_title, title)!=0)
                {
                    if(ctx->window_title) free(ctx->window_title); ctx->window_title = title;
                    Xutf8SetWMProperties(display, win, ctx->window_title, NULL, NULL, 0, NULL, NULL, NULL);
                }
                else
                {
                    free(title);
                }
            }
            else
            {
                if(ctx->window_title) free(ctx->window_title); ctx->window_title = NULL;
                Xutf8SetWMProperties(display, win, ctx->window_title, NULL, NULL, 0, NULL, NULL, NULL);
            }
        }
        k_draw_window_intern(ctx,0);
    }
}

void k_set_title(k_context* ctx, DWORD titleaddr, int cp)
{
    if(ctx->window_title) free(ctx->window_title);
    ctx->window_title = titleaddr==0 ? NULL : (char*)k_strdup(user_pb(titleaddr),-1,3);
    if(win!=0)
    {
        Xutf8SetWMProperties(display, win, ctx->window_title, NULL, NULL, 0, NULL, NULL, NULL);
        k_event_redraw(ctx);
    }
}

void k_draw_button(k_context* ctx, int x, int y, DWORD width, DWORD height, DWORD color)
{
    if(win==0) return; k_clear_redraw(ctx);
    x += ctx->client_x; y += ctx->client_y;
    XGCValues val; val.foreground = (color &= 0xFFFFFF); DWORD color2 = (color>>1)&0x7F7F7F;
    GC gc = XCreateGC(display, win, GCForeground, &val);
    XFillRectangle(display, win, gc, x+2, y+2, width-3, height-3);
    XSetForeground(display, gc, color2);
    XFillRectangle(display, win, gc, x+1, y, width-1, 1);
    XFillRectangle(display, win, gc, x, y+1, 1, height-1);
    XFillRectangle(display, win, gc, x+1, y+height, width-1, 1);
    XFillRectangle(display, win, gc, x+width, y+1, 1, height-1);
    XSetForeground(display, gc, (color>>1)|0x808080);
    XFillRectangle(display, win, gc, x+1, y+1, width-1, 1);
    XFillRectangle(display, win, gc, x+1, y+2, 1, height-3);
    XSetForeground(display, gc, color2+((color2>>1)&0x7F7F7F));
    XFillRectangle(display, win, gc, x+1, y+height-1, width-1, 1);
    XFillRectangle(display, win, gc, x+width-1, y+2, 1, height-3);
    XFreeGC(display, gc);
}

void k_draw_pixel(k_context* ctx, int x, int y, DWORD color)
{
    if(win==0) return; k_clear_redraw(ctx);
    x += ctx->client_x; y += ctx->client_y;
    XGCValues val;
    if (((color>>24)&1)==0)
    {
        val.function = GXcopy;
        val.foreground = color;
    }
    else
    {
        val.function = GXxor;
        val.foreground = 0xFFFFFF;
    }
    GC gc = XCreateGC(display, win, GCFunction|GCForeground, &val);
    XFillRectangle(display, win, gc, x,y, 1,1);
    XFreeGC(display, gc);
}

void k_draw_rect(k_context* ctx, int x, int y, DWORD width, DWORD height, DWORD color)
{
    if(win==0) return; k_clear_redraw(ctx);
    x += ctx->client_x; y += ctx->client_y;
    k_fill_rectangle(ctx,x,y,width,height,color);
}

void k_draw_line(k_context* ctx, int x1, int y1, int x2, int y2, DWORD color)
{
    if(win==0) return; k_clear_redraw(ctx);
    x1 += ctx->client_x; y1 += ctx->client_y; x2 += ctx->client_x; y2 += ctx->client_y;
    XGCValues val;
    if (((color>>24)&1)==0)
    {
        val.function = GXcopy;
        val.foreground = color;
    }
    else
    {
        val.function = GXxor;
        val.foreground = 0xFFFFFF;
    }
    GC gc = XCreateGC(display, win, GCFunction|GCForeground, &val);
    XDrawLine(display, win, gc, x1,y1, x2,y2);
    XFreeGC(display, gc);
}

void k_draw_text(k_context* ctx, int x, int y, BYTE* text, int len, DWORD color, DWORD extra)
{
    if(win==0) return; k_clear_redraw(ctx);
    x += ctx->client_x; y += ctx->client_y;
    k_draw_text_intern(ctx,x,y,text,color>>31?-1:len,(color>>30)&1,(color>>28)&3,(color>>27)&1,(color>>24)&7,color,extra);
}

char* k_itoa(int n, char* buf, int radix, int width, int trim)
{
    static char digit[] = "0123456789ABCDEF";
    int sign = n<0; if(sign) n=-n;
    *buf = digit[n%radix];
    while((n>=radix||trim==0) && --width>0)
    {
        n /= radix; *--buf = digit[n%radix];
    }
    if(sign) *--buf = '-';
    return buf;
}

void k_draw_num(k_context* ctx, int x, int y, DWORD color, DWORD options, void* pnum, DWORD extra)
{
    static char bases[] = {10,16,2,8}; char buf[63]; buf[63]=0;
    int width = (options>>16)&63, radix = bases[(options>>8)&3];
    char* num = k_itoa(*(int*)pnum,buf+62,radix,width,(options>>31)&1);
    k_draw_text(ctx,x,y,(BYTE*)num,-1,color,extra);
}

#define B5(s1,s2) ((((((*(WORD*)img)>>s1)&31)<<3)|(((*(WORD*)img)>>(s1+5))&7))<<s2)
#define B6(s1,s2) ((((((*(WORD*)img)>>s1)&63)<<2)|(((*(WORD*)img)>>(s1+6))&3))<<s2)

void k_draw_image(k_context* ctx, int x, int y, DWORD width, DWORD height, BYTE* img, DWORD bpp, DWORD pitch, DWORD* pal32)
{
    if(win==0) return; k_clear_redraw(ctx);
    x += ctx->client_x; y += ctx->client_y;
    DWORD* image32 = (DWORD*)malloc(width*height*4),i,j,p; int k;
    switch(bpp)
    {
    case 1:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++img)
                for(k = 7; j < width && k >= 0; ++j,--k,++p) image32[p] = pal32[(*img>>k)&1];
        break;
    case 2:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++img)
                for(k = 6; j < width && k >= 0; ++j,k-=2,++p) image32[p] = pal32[(*img>>k)&3];
        break;
    case 4:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++img)
                for(k = 4; j < width && k >= 0; ++j,k-=4,++p) image32[p] = pal32[(*img>>k)&15];
        break;
    case 8:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++j,++p,++img) image32[p] = pal32[*img];
        break;
    case 9:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++j,++p,++img) image32[p] = ((*img)<<16)|((*img)<<8)|*img;
        break;
    case 15:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++j,++p,img+=2) image32[p] = B5(10,16)|B5(5,8)|B5(0,0);
        break;
    case 16:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++j,++p,img+=2) image32[p] = B5(11,16)|B6(5,8)|B5(0,0);
        break;
    case 24:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++j,++p,img+=3) image32[p] = 0xffffff & *(DWORD*)img;
        break;
    case 32:
        for(p = i = 0; i < height; ++i,img+=pitch)
            for(j = 0; j < width; ++j,++p,img+=4) image32[p] = 0xffffff & *(DWORD*)img;
        break;
    default:
        k_panic("bpp not supported");
    }
    XImage* ximg = XCreateImage(display, DefaultVisual(display, 0), 24, ZPixmap, 0, (char*)image32, width, height, 32, 0);
    XPutImage(display, win, DefaultGC(display, 0), ximg, 0,0, x,y, width,height);
    XDestroyImage(ximg);
}
