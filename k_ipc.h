#pragma pack(push,1)
typedef struct
{
    WORD args;
    char buf[4096-6];
} msg_run_t;

typedef struct
{
    DWORD slot;
    DWORD eip;
    DWORD esp;
} msg_clone_t;

typedef struct
{
    int retcode;
} msg_reply_t;

typedef struct
{
    DWORD from_slot;
    DWORD to_slot;
    DWORD code;
    DWORD len;
    BYTE data[4096-20];
} msg_ipc_t;

typedef struct
{
    DWORD from_slot;
    DWORD to_slot;
    DWORD code;
    DWORD type;
    DWORD param;
} msg_ipc_event_t;

typedef struct
{
    DWORD slot;
} msg_focus_t;

typedef struct
{
    DWORD shmid;
    DWORD handle;
    DWORD code;
    DWORD iaddr;
    DWORD ilen;
    DWORD oaddr;
    DWORD olen;
} msg_ioctl_t;

typedef struct
{
    WORD len;
    WORD type;
    union
    {
        msg_run_t run;
        msg_clone_t clone;
        msg_reply_t reply;
        msg_ipc_t ipc;
        msg_ipc_event_t ipc_event;
        msg_focus_t focus;
        msg_ioctl_t ioctl;
    } u;
} msg_t;
#pragma pack(pop)

extern int ipc_server;

#define MSGTYPE_RUN   1
#define MSGTYPE_CLONE 2
#define MSGTYPE_REPLY 3
#define MSGTYPE_IPC   4
#define MSGTYPE_GOT_FOCUS 5
#define MSGTYPE_LOAD_SKIN 6
#define MSGTYPE_DRV_LOAD  7
#define MSGTYPE_DRV_IOCTL 8

#define IPCCODE_DATA     256
#define IPCCODE_ACTIVATE 257
#define IPCCODE_EVENT    258

int read_msg(int fd, msg_t* msg);
int write_msg(int fd, msg_t* msg);
void k_process_ipc_event(k_context* ctx, msg_t* msg);
void k_focus_in(k_context* ctx);

void msg_run(msg_t* msg, char* name, char* args);
void msg_clone(msg_t* msg, DWORD slot, DWORD eip, DWORD esp);
void msg_reply(msg_t* msg, int retcode);
void msg_ipc(msg_t* msg, DWORD slot, DWORD code, DWORD len, BYTE* data);
void msg_ipc_event(msg_t* msg, DWORD slot, DWORD type, DWORD param);
void msg_load_skin(msg_t* msg, char* fname);
void msg_driver_load(msg_t* msg, char* name);
void msg_driver_ioctl(msg_t* msg, DWORD shmid, DWORD* args);
