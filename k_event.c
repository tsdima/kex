#include "k_mem.h"
#include "k_event.h"

#include <string.h>

#define MAX_BUTTONS 256

#define g_mouse_x kernel_mem()->mouse_x
#define g_mouse_y kernel_mem()->mouse_y

#define g_modifiers kernel_mem()->keyboard_modifiers
#define g_keys kernel_mem()->keyboard_buffer
#define g_in_pos kernel_mem()->keyboard_in_pos
#define g_out_pos kernel_mem()->keyboard_out_pos
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

DWORD k_get_key(k_context* ctx)
{
    if (g_in_pos == g_out_pos) return 1;
    DWORD key = g_keys[g_out_pos++]; g_out_pos %= MAX_KEYS;
    if (g_in_pos == g_out_pos) ctx->event_pending &= ~K_EVMASK_KEY;
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

void k_event_mousemove(k_context* ctx, int x, int y)
{
    g_mouse_x = x; g_mouse_y = y;
    ctx->event_pending |= K_EVMASK_MOUSE;
    ctx->mouse_x = x - ctx->window_x;
    ctx->mouse_y = y - ctx->window_y;
}

void k_event_mousepress(k_context* ctx, DWORD button)
{
    ctx->event_pending |= K_EVMASK_MOUSE;
    ctx->mouse_state |= (button<<8)|button;
    ctx->mouse_last_pressed = button&0xFE;
    k_button* b = k_find_button(ctx);
    if(b!=NULL) ctx->button_id_pressed = b->id;
}

void k_event_mouserelease(k_context* ctx, DWORD button)
{
    ctx->event_pending |= K_EVMASK_MOUSE;
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
