typedef short SHORT;
typedef int   LONG;

typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef unsigned int   DWORD;
typedef unsigned long long QWORD;

#define MAX_CHILD 64
#define MAX_KEYS 120
#define MAX_HOTKEYS 64
#define MAX_USM 16
#define MAX_DRIVER 16
#define MAX_SOCKET 32
#define MAX_IFACE 8
#define DEBUG_BOARD_LEN 4096

typedef struct
{
    DWORD tv_sec;
    DWORD tv_nsec;
} k_timespec;

typedef struct
{
    DWORD pid;
    DWORD tid;
    DWORD shmid;
    DWORD memsize;
    DWORD event_mask;
    DWORD event_pending;
    DWORD mouse_state;
    DWORD mouse_last_pressed;
    DWORD kbd_mode;
    DWORD hotkey_out_pos;
    DWORD hotkey_count;
    DWORD hotkey_def[MAX_HOTKEYS];
    DWORD window_color;
    DWORD window_zpos_me;
    DWORD window_zpos;
    DWORD window_state;
    DWORD window_drag_mode;
    char* window_title;
    int window_x;
    int window_y;
    int window_w;
    int window_h;
    int orig_x;
    int orig_y;
    int orig_w;
    int orig_h;
    int client_x;
    int client_y;
    int mouse_x;
    int mouse_y;
    int mouse_wheel_y;
    int button_id_pressed;
    int button_id;
    int focused;
    int retcode;
    BYTE* ipc_buffer;
    DWORD ipc_buf_len;
    char name[12];
    BYTE curpath[256];
    DWORD usm_addr[MAX_USM];
    DWORD usm_size[MAX_USM];
    int sockets[MAX_SOCKET];
} k_context;

typedef struct
{
    DWORD tcount;
    DWORD flags;
    char name[32];
} k_user_shm;

typedef struct
{
    char name[16];
    DWORD (*ioctl)(DWORD code, void* idata, DWORD ilen, void* odata, DWORD olen);
} k_driver;

typedef struct
{
    char  name[32];
    DWORD mac_lo;
    DWORD mac_hi;
    DWORD ip;
    DWORD mask;
    DWORD dns;
    DWORD gateway;
} k_iface;

typedef struct
{
    DWORD keyboard_modifiers;
    DWORD keyboard_country;
    DWORD keyboard_language;
    DWORD keyboard_in_pos;
    DWORD keyboard_out_pos;
    DWORD keyboard_buffer[MAX_KEYS];
    DWORD hotkey_in_pos;
    DWORD hotkey_buffer[MAX_KEYS];
    char keyboard_layout[3][128];
    int mouse_x;
    int mouse_y;
    int mouse_dbl_click_delay;
    k_timespec mouse_dbl_click_timeout;
    WORD desktop_left;
    WORD desktop_top;
    WORD desktop_right;
    WORD desktop_bottom;
    DWORD active_slot;
    DWORD debug_in_pos;
    DWORD debug_out_pos;
    int shmtc[MAX_CHILD];
    k_context slot[MAX_CHILD];
    k_user_shm usm[MAX_USM];
    k_driver driver[MAX_DRIVER];
    char extfs_key[64];
    char extfs_path[64];
    DWORD if_count;
    k_iface iface[MAX_IFACE];
    DWORD pci_enabled;
    DWORD clipboard_count;
    BYTE debug_board[DEBUG_BOARD_LEN];
} KERNEL_MEM;

void k_printf(char* fmt, ...);
void k_panic(char* msg);

void k_kernel_mem_init();
void k_kernel_mem_cleanup();
KERNEL_MEM* kernel_mem();

#define g_slot kernel_mem()->slot

void k_mem_init(int shmid);
void k_mem_done(int shmid);
void k_mem_reopen();
void* k_mem_alloc(DWORD size);
void* k_mem_alloc_from_heap(DWORD size);
DWORD k_mem_size(DWORD size);
DWORD k_mem_get_size();

DWORD k_heap_init();
DWORD k_heap_alloc(DWORD size);
DWORD k_heap_realloc(DWORD addr, DWORD size);
DWORD k_heap_free(DWORD addr);

DWORD k_usm_open(k_context* ctx, DWORD aname, DWORD size, DWORD flags, DWORD* addr);
DWORD k_usm_close(k_context* ctx, DWORD aname);
void  k_usm_clean(k_context* ctx);

void* user_mem(DWORD addr);

#define user_pb(addr) ((BYTE*)user_mem(addr))
#define user_pw(addr) ((WORD*)user_mem(addr))
#define user_pd(addr) ((DWORD*)user_mem(addr))
#define k_get_byte(addr) (*user_pb(addr))
#define k_get_word(addr) (*user_pw(addr))
#define k_get_dword(addr) (*user_pd(addr))

void* k_skin_alloc(DWORD size);
BYTE* k_skin_open();
void  k_skin_close();

DWORD k_clipboard_add(DWORD size, DWORD addr);
DWORD k_clipboard_get(DWORD id);
DWORD k_clipboard_remove_last();

DWORD k_stub_resume(DWORD eip);
DWORD k_stub_jmp(DWORD eip, DWORD esp);

void k_save_fsbase();
void k_load_fsbase();
void k_set_fsbase();

BYTE* k_mem_open_app(int shmid, DWORD* psize);
void k_mem_close_app(void* ptr, DWORD size);
