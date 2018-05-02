#include "k_mem.h"
#include "k_event.h"

#include <string.h>
#include <time.h>

#define MAX_BUTTONS 256

#define g_mouse_x kernel_mem()->mouse_x
#define g_mouse_y kernel_mem()->mouse_y
#define g_mouse_t kernel_mem()->mouse_dbl_click_timeout

#define g_modifiers kernel_mem()->keyboard_modifiers
#define g_keys kernel_mem()->keyboard_buffer
#define g_in_pos kernel_mem()->keyboard_in_pos
#define g_out_pos kernel_mem()->keyboard_out_pos
#define g_hotkeys kernel_mem()->hotkey_buffer
#define g_hot_in_pos kernel_mem()->hotkey_in_pos
#define g_layout kernel_mem()->keyboard_layout
#define g_language kernel_mem()->keyboard_language
#define g_country kernel_mem()->keyboard_country

k_button button_list[MAX_BUTTONS];

DWORD k_check_event(k_context* ctx)
{
    DWORD bit = ctx->event_pending & ctx->event_mask,m,i; if (bit==0) return 0;
    for(m=i=1; i<32; ++i,m<<=1) if((bit&m)!=0)
    {
        if((K_EVMASK_AUTORESET&m)!=0) ctx->event_pending &= ~m;
        return i;
    }
    return 0;
}

void k_event_redraw(k_context* ctx)
{
    ctx->event_pending |= K_EVMASK_REDRAW;
}

void k_clear_redraw(k_context* ctx)
{
    ctx->event_pending &= ~K_EVMASK_REDRAW;
}

void k_event_keypress(k_context* ctx, DWORD key)
{
    ctx->event_pending |= K_EVMASK_KEY;
    g_keys[g_in_pos++] = key; g_in_pos %= MAX_KEYS;
}

DWORD k_define_hotkey(k_context* ctx, DWORD def)
{
    if(ctx->hotkey_count>=MAX_HOTKEYS) return 1;
    ctx->hotkey_def[ctx->hotkey_count++] = def;
    return 0;
}

DWORD k_remove_hotkey(k_context* ctx, DWORD def)
{
    int i;
    for(i=0; i<ctx->hotkey_count; ++i) if(ctx->hotkey_def[i]==def)
    {
        if(i<--ctx->hotkey_count) memmove(ctx->hotkey_def, ctx->hotkey_def+1, sizeof(DWORD)*(ctx->hotkey_count-i));
        return 0;
    }
    return 1;
}

int k_is_hotkey(DWORD key, DWORD def)
{
    static DWORD m1[]={0,1,3,1,2},m2[]={0,2,3,1,2};
    if((key&0xFF)!=(def&0xFF)) return 0;
    DWORD i; key>>=8; def>>=8;
    for(i=0; i<3; ++i,key>>=2,def>>=4)
    {
        DWORD m=key&3, z=def&15; if(z>4) z=0;
        if(m!=m1[z] && m!=m2[z]) return 0;
    }
    return 1;
}

int k_check_hotkey(DWORD scancode)
{
    k_context* ctx; int i,is_hot=0; scancode |= (g_modifiers&KMOD_CTRALTSH)<<8;
    for(ctx = g_slot; ctx<g_slot+MAX_CHILD; ++ctx) if(ctx->tid)
    {
        for(i=0; i<ctx->hotkey_count; ++i)
        {
            if(k_is_hotkey(scancode, ctx->hotkey_def[i]))
            {
                is_hot = 1; ctx->event_pending |= K_EVMASK_KEY;
                break;
            }
        }
    }
    if(is_hot)
    {
        g_hotkeys[g_hot_in_pos++] = scancode; g_hot_in_pos %= MAX_KEYS;
    }
    return is_hot;
}

DWORD k_get_key(k_context* ctx)
{
    if(g_hot_in_pos<MAX_KEYS) while(g_hot_in_pos != ctx->hotkey_out_pos)
    {
        DWORD i,key = g_hotkeys[ctx->hotkey_out_pos++]; ctx->hotkey_out_pos %= MAX_KEYS;
        for(i=0; i<ctx->hotkey_count; ++i) if(k_is_hotkey(key, ctx->hotkey_def[i]))
        {
            if (g_hot_in_pos != ctx->hotkey_out_pos) ctx->event_pending |= K_EVMASK_KEY;
            return (key<<8)|2;
        }
    }
    if (g_in_pos == g_out_pos) return 1;
    DWORD key = g_keys[g_out_pos++]; g_out_pos %= MAX_KEYS;
    if (g_in_pos != g_out_pos) ctx->event_pending |= K_EVMASK_KEY;
    return key;
}

void k_clear_buttons(k_context* ctx)
{
    int i;
    for(i=0; i<MAX_BUTTONS; ++i)
    {
        if (button_list[i].ctx==ctx) button_list[i].ctx = NULL;
    }
}

void k_define_button(k_context* ctx, int x, int y, int w, int h, int id)
{
    int i;
    for(i=0; i<MAX_BUTTONS; ++i)
    {
        if (button_list[i].ctx==ctx && button_list[i].x==x && button_list[i].y==y
             && button_list[i].w==w && button_list[i].h==h) break;
        if (button_list[i].ctx==NULL)
        {
            button_list[i].ctx = ctx;
            button_list[i].x = x;
            button_list[i].y = y;
            button_list[i].w = w;
            button_list[i].h = h;
            button_list[i].id = id;
            break;
        }
    }
}

void k_remove_button(k_context* ctx, int id)
{
    int i; id &= 0xFFFFFF;
    for(i=0; i<MAX_BUTTONS; ++i)
    {
        if (button_list[i].ctx==ctx && (button_list[i].id&0xFFFFFF)==id) button_list[i].ctx = NULL;
    }
}

k_button* k_find_button_by_id(k_context* ctx, int id)
{
    int i; id &= 0xFFFFFF;
    for(i=0; i<MAX_BUTTONS; ++i)
    {
        if (button_list[i].ctx==ctx && (button_list[i].id&0xFFFFFF)==id) return button_list+i;
    }
    return NULL;
}

k_button* k_find_button(k_context* ctx)
{
    int i,x=ctx->mouse_x-1,y=ctx->mouse_y-1;
    for(i=0; i<MAX_BUTTONS; ++i)
    {
        if (button_list[i].ctx==ctx
            && x>button_list[i].x
            && y>button_list[i].y
            && x<=button_list[i].x+button_list[i].w
            && y<=button_list[i].y+button_list[i].h
            ) return button_list+i;
    }
    return NULL;
}

void k_event_mouse(k_context* ctx)
{
    DWORD m = ctx->event_mask>>30;
    if((m&2)!=0 && ctx->focused==0) return;
    if((m&1)!=0 && (ctx->mouse_x<0||ctx->mouse_y<0||ctx->mouse_x>=ctx->window_w||ctx->mouse_y>=ctx->window_h)) return;
    ctx->event_pending |= K_EVMASK_MOUSE;
}

void k_event_mousemove(k_context* ctx, int x, int y)
{
    g_mouse_x = x; g_mouse_y = y;
    for(ctx = g_slot; ctx<g_slot+MAX_CHILD; ++ctx) if(ctx->tid)
    {
        ctx->mouse_x = x - ctx->window_x;
        ctx->mouse_y = y - ctx->window_y;
        k_event_mouse(ctx);
    }
}

void k_event_mousepress(k_context* ctx, DWORD button, int wheel)
{
    k_event_mouse(ctx);
    ctx->mouse_state |= (button<<8)|button|(wheel!=0?0x8000:0);
    ctx->mouse_last_pressed = button&0xFE;
    ctx->mouse_wheel_y += wheel;
    k_button* b = k_find_button(ctx);
    if(b!=NULL) ctx->button_id_pressed = b->id;
    int dbl = k_is_dblclick(ctx);
    k_time_get(&g_mouse_t);
    if(!dbl) k_time_add_ms(&g_mouse_t, kernel_mem()->mouse_dbl_click_delay*10);
}

void k_event_mouserelease(k_context* ctx, DWORD button)
{
    k_event_mouse(ctx);
    ctx->mouse_state |= button<<16;
    ctx->mouse_state &= ~button;
    k_button* b = k_find_button(ctx);
    if(b!=NULL && ctx->button_id_pressed == b->id && b->id!=0xFFFF)
    {
        ctx->event_pending |= K_EVMASK_BUTTON;
        ctx->button_id = b->id;
    }
    ctx->button_id_pressed = 0;
}

DWORD k_get_mousewheel(k_context* ctx)
{
    int y = ctx->mouse_wheel_y; ctx->mouse_wheel_y = 0;
    return y;
}

DWORD k_get_mousepos(k_context* ctx, int* x, int* y)
{
    int mx = ctx->mouse_x - ctx->client_x;
    int my = ctx->mouse_y - ctx->client_y;
    ctx->event_pending &= ~K_EVMASK_MOUSE;
    if(x) *x = mx;
    if(y) *y = my;
    return (mx<<16) + my;
}

DWORD k_get_mouse(int* x, int* y)
{
    if(x) *x = g_mouse_x;
    if(y) *y = g_mouse_y;
    return (g_mouse_x<<16) + g_mouse_y;
}

DWORD k_get_mousestate(k_context* ctx)
{
    DWORD ret = ctx->mouse_state;
    ctx->event_pending &= ~K_EVMASK_MOUSE;
    ctx->mouse_state &= 0xFF;
    return ret;
}

int k_is_dblclick(k_context* ctx)
{
    k_timespec now; k_time_get(&now);
    return k_time_gt(&g_mouse_t, &now);
}

DWORD k_get_button(k_context* ctx)
{
    if((ctx->event_pending&K_EVMASK_BUTTON)==0) return 1;
    ctx->event_pending &= ~K_EVMASK_BUTTON;
    return (ctx->button_id<<8)|ctx->mouse_last_pressed;
}

void k_set_keyboard_modifiers(DWORD modifiers, int reset)
{
    if(reset) g_modifiers &= ~modifiers; else g_modifiers |= modifiers;
}

DWORD k_get_keyboard_modifiers()
{
    return g_modifiers;
}

DWORD k_get_keyboard_layout(DWORD id, DWORD data)
{
    if(id>0 && id<4)
    {
        memcpy(user_mem(data), g_layout[id-1], 128);
    }
    return g_country;
}

DWORD k_set_keyboard_layout(DWORD id, DWORD data)
{
    if(id>0 && id<4)
    {
        memcpy(g_layout[id-1], user_mem(data), 128);
        return 0;
    }
    else if(id==9)
    {
        g_country = data;
        return 0;
    }
    return 1;
}

DWORD k_get_keyboard_lang()
{
    return g_language;
}

DWORD k_set_keyboard_lang(DWORD id)
{
    g_language = id;
    return 0;
}

void k_event_ipc(k_context* ctx)
{
    ctx->event_pending |= K_EVMASK_IPC;
}

void k_clear_ipc(k_context* ctx)
{
    ctx->event_pending &= ~K_EVMASK_IPC;
}

void k_event_network(k_context* ctx)
{
    ctx->event_pending |= K_EVMASK_NETWORK;
}

void k_time_get(k_timespec* time)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    time->tv_sec = ts.tv_sec;
    time->tv_nsec = ts.tv_nsec;
}

void k_time_add_ms(k_timespec* time, int ms)
{
    ms += time->tv_nsec/1000000;
    time->tv_sec += ms/1000; ms %= 1000;
    time->tv_nsec = time->tv_nsec%1000000 + ms*1000000;
}

int k_time_gt(k_timespec* a, k_timespec* b)
{
    return a->tv_sec > b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec > b->tv_nsec);
}

DWORD bcd(DWORD n)
{
    return n<10 ? n : (bcd(n/10)<<4)|(n%10);
}

DWORD k_bcd_time()
{
    time_t ltime; struct tm* ltm;
    ltime = time(NULL); ltm = localtime(&ltime);
    return (((bcd(ltm->tm_sec)<<8)|bcd(ltm->tm_min))<<8)|bcd(ltm->tm_hour);
}

DWORD k_bcd_date()
{
    time_t ltime; struct tm* ltm;
    ltime = time(NULL); ltm = localtime(&ltime);
    return (((bcd(ltm->tm_mday)<<8)|bcd(ltm->tm_mon+1))<<8)|bcd(ltm->tm_year%100);
}
