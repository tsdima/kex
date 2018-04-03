#define K_EVENT_REDRAW  1
#define K_EVENT_KEY     2
#define K_EVENT_BUTTON  3
#define K_EVENT_DESKTOP 5
#define K_EVENT_MOUSE   6
#define K_EVENT_IPC     7
#define K_EVENT_NETWORK 8
#define K_EVENT_DEBUG   9

#define K_EVMASK_REDRAW  1
#define K_EVMASK_KEY     2
#define K_EVMASK_BUTTON  4
#define K_EVMASK_DESKTOP 0x10
#define K_EVMASK_MOUSE   0x20
#define K_EVMASK_IPC     0x40
#define K_EVMASK_NETWORK 0x80
#define K_EVMASK_DEBUG   0x100
#define K_EVMASK_AUTORESET (K_EVMASK_MOUSE|K_EVMASK_IPC|K_EVMASK_NETWORK|K_EVMASK_DEBUG)

#define KBS_NO_PRESS 0x20000000
#define KBS_NO_DRAW  0x40000000

#define KMOD_LSHIFT   1
#define KMOD_RSHIFT   2
#define KMOD_LCTRL    4
#define KMOD_RCTRL    8
#define KMOD_LALT     0x10
#define KMOD_RALT     0x20
#define KMOD_CAPSLOCK 0x40
#define KMOD_NUMLOCK  0x80
#define KMOD_SCROLL   0x100
#define KMOD_LOCKMASK 0x1C0
#define KMOD_LWIN     0x200
#define KMOD_RWIN     0x400

typedef struct
{
    k_context* ctx;
    int x,y,w,h,id;
} k_button;

DWORD k_check_event(k_context* ctx);

void k_event_redraw(k_context* ctx);
void k_event_mousemove(k_context* ctx, int x, int y);
void k_event_mousepress(k_context* ctx, DWORD button);
void k_event_mouserelease(k_context* ctx, DWORD button);
void k_event_keypress(k_context* ctx, DWORD key);
void k_event_network(k_context* ctx);

void k_clear_redraw(k_context* ctx);
DWORD k_get_mousepos(k_context* ctx, int* x, int* y);
DWORD k_get_mouse(int* x, int* y);
DWORD k_get_mousestate(k_context* ctx);
DWORD k_get_key(k_context* ctx);
DWORD k_get_button(k_context* ctx);

void k_clear_buttons(k_context* ctx);
void k_define_button(k_context* ctx, int x, int y, int w, int h, int id);
void k_remove_button(k_context* ctx, int id);

k_button* k_find_button(k_context* ctx);
k_button* k_find_button_by_id(k_context* ctx, int id);

void  k_set_keyboard_modifiers(DWORD modifiers, int reset);
DWORD k_get_keyboard_modifiers();
DWORD k_get_keyboard_layout(DWORD id, DWORD data);
DWORD k_set_keyboard_layout(DWORD id, DWORD data);
DWORD k_get_keyboard_lang();
DWORD k_set_keyboard_lang(DWORD id);

void k_event_ipc(k_context* ctx);
void k_clear_ipc(k_context* ctx);
