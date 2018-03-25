#pragma pack(push,1)
typedef struct
{
    DWORD cputime;
    WORD zpos_me;
    WORD zpos;
    WORD reserved1;
    char name[12];
    DWORD base;
    DWORD last;
    DWORD pid;
    DWORD window_x;
    DWORD window_y;
    DWORD window_w;
    DWORD window_h;
    WORD state;
    WORD reserved2;
    DWORD client_x;
    DWORD client_y;
    DWORD client_w;
    DWORD client_h;
    BYTE window_state;
    DWORD event_mask;
    BYTE kbd_mode;
} K_SLOT_INFO;
#pragma pack(pop)

extern int base_pid;

void k_syscall_init();
void k_set_slot(DWORD slot, int shmid, char* name);
void k_set_slot_from(DWORD slot, DWORD from);
DWORD k_max_slot();
DWORD k_find_slot(DWORD tid);
DWORD k_get_slot_by_point(int x, int y);
