#pragma pack(push,1)
typedef struct
{
    DWORD magic;
    DWORD version;
    DWORD params;
    DWORD buttons;
    DWORD bitmaps;
} K_SKIN_HDR;

typedef struct
{
    DWORD inner;
    DWORD outer;
    DWORD frame;
} K_SKIN_COLORS;

typedef struct
{
    DWORD height;
    WORD rmargin;
    WORD lmargin;
    WORD bmargin;
    WORD tmargin;
    K_SKIN_COLORS acolor;
    K_SKIN_COLORS icolor;
    DWORD dtpfsize;
    DWORD dtp[10];
} K_SKIN_PARAMS;

typedef struct
{
    DWORD type; // 0-EOL, 1-close, 2-minimize
    SHORT x;
    SHORT y;
    WORD width;
    WORD height;
} K_SKIN_BUTTON;

typedef struct
{
    WORD kind; // 0-EOL, 1-left, 2-oper, 3-base
    WORD type; // 0-inactive, 1-active
    DWORD offset;
} K_SKIN_BMPDEF;

typedef struct
{
    DWORD width;
    DWORD height;
    BYTE data[1];
} K_SKIN_BITMAP;
#pragma pack(pop)

typedef struct
{
    BYTE width;
    BYTE height;
    WORD chars;
    BYTE* bmp;
} k_bitmap_font;

extern k_bitmap_font font9,font16;

void k_gui_init();
void k_process_event(k_context* ctx);
void k_move_mouse(int x, int y);
void k_move_window(k_context* ctx, int x, int y);
void k_move_size_window(k_context* ctx, int x, int y, int width, int height);
void k_raise_window(k_context* ctx);
void k_window(k_context* ctx, int x, int y, DWORD width, DWORD height, DWORD color, DWORD titleaddr);
void k_set_title(k_context* ctx, DWORD titleaddr, int cp);
void k_draw_pixel(k_context* ctx, int x, int y, DWORD color);
void k_draw_button(k_context* ctx, int x, int y, DWORD width, DWORD height, DWORD color);
void k_draw_rect(k_context* ctx, int x, int y, DWORD width, DWORD height, DWORD color);
void k_draw_line(k_context* ctx, int x1, int y1, int x2, int y2, DWORD color);
void k_draw_text(k_context* ctx, int x, int y, BYTE* text, int len, DWORD color, DWORD extra);
void k_draw_num(k_context* ctx, int x, int y, DWORD color, DWORD options, void* pnum, DWORD extra);
void k_draw_image(k_context* ctx, int x, int y, DWORD width, DWORD height, BYTE* img, DWORD bpp, DWORD pitch, DWORD* pal32);
void k_get_screen_size(DWORD* width, DWORD* height);
void k_get_desktop_rect(DWORD* left, DWORD* top, DWORD* right, DWORD* bottom);
void k_set_desktop_rect(WORD left, WORD top, WORD right, WORD bottom);
DWORD k_get_pixel(k_context* ctx, int x, int y);
void k_get_image(k_context* ctx, int x, int y, DWORD width, DWORD height, BYTE* img);

void k_set_skin(BYTE* skin);
DWORD k_get_skin_height();
void k_get_skin_colors(BYTE* buf, DWORD size);
void k_get_client_rect(k_context* ctx, DWORD* x, DWORD* y, DWORD* w, DWORD* h);
