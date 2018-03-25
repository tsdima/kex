#include "k_mem.h"
#include "k_event.h"
#include "k_gui.h"
#include "k_ipc.h"

#include <string.h>
#include <sys/socket.h>

int ipc_server = -1;

int read_msg(int fd, msg_t* msg)
{
    int len = sizeof(DWORD);
    BYTE *buf = (BYTE*)msg, *p = buf;
    while(p<buf+len)
    {
        int size = recv(fd, p, len-(p-buf), 0); if(size<=0) return 0;
        p += size; if(len==sizeof(DWORD) && p-buf>=sizeof(msg->len)) len = msg->len;
    }
    return 1;
}

int write_msg(int fd, msg_t* msg)
{
    int len = msg->len;
    BYTE *buf = (BYTE*)msg, *p=buf;
    while(p<buf+len)
    {
        int size = send(fd, p, len-(p-buf), 0); if(size<=0) return 0;
        p += size;
    }
    return 1;
}

void k_process_ipc_event(k_context* ctx, msg_t* msg)
{
    int state=0,err; DWORD* pdw=(DWORD*)ctx->ipc_buffer;
    do
    {
        read_msg(ipc_server, msg);
        switch(msg->type)
        {
        case MSGTYPE_REPLY: ctx->retcode = msg->u.reply.retcode; break;
        case MSGTYPE_IPC:
            switch(state)
            {
            case 0:
                switch(msg->u.ipc.code)
                {
                case IPCCODE_DATA:
                    if(pdw == NULL) err = 1; else
                    if(pdw[0] != 0) err = 2; else
                    if(pdw[1]+msg->u.ipc.len+8 > ctx->ipc_buf_len) err = 3; else
                    {
                        err = 0; state = 1;
                        *(DWORD*)(ctx->ipc_buffer+pdw[1]) = g_slot[msg->u.ipc.from_slot].tid; pdw[1] += 4;
                        *(DWORD*)(ctx->ipc_buffer+pdw[1]) = msg->u.ipc.len; pdw[1] += 4;
                    }
                    msg_ipc(msg, msg->u.ipc.from_slot, err, 0, NULL);
                    write_msg(ipc_server, msg);
                    break;
                case IPCCODE_ACTIVATE:
                    k_raise_window(ctx);
                    break;
                case IPCCODE_EVENT:
                    switch(msg->u.ipc_event.type)
                    {
                    case 2: k_event_keypress(ctx, msg->u.ipc_event.param<<8); break;
                    case 3: ctx->button_id = msg->u.ipc_event.param; ctx->event_pending |= K_EVMASK_BUTTON; break;
                    }
                    break;
                }
                break;
            case 1:
                memcpy(ctx->ipc_buffer+pdw[1], msg->u.ipc.data, msg->u.ipc.len); pdw[1] += msg->u.ipc.len;
                if(msg->u.ipc.code!=IPCCODE_DATA)
                {
                    msg_ipc(msg, msg->u.ipc.from_slot, 0, 0, NULL);
                    write_msg(ipc_server, msg);
                    k_event_ipc(ctx); state = 0;
                }
                break;
            }
            break;
        case MSGTYPE_LOAD_SKIN:
            k_set_skin(k_skin_open());
            k_event_redraw(ctx);
            break;
        }
    }
    while(state != 0);
}

void msg_run(msg_t* msg, char* fname, char* args)
{
    msg->len = strlen(fname)+7;
    msg->type = MSGTYPE_RUN;
    msg->u.run.args = 0;
    strcpy(msg->u.run.buf, fname);
    if(args)
    {
        msg->u.run.args = msg->len-6;
        strcpy(msg->u.run.buf+msg->u.run.args, args);
        msg->len += strlen(args)+1;
    }
}

void msg_clone(msg_t* msg, DWORD slot, DWORD eip, DWORD esp)
{
    msg->len = sizeof(msg->u.clone)+4;
    msg->type = MSGTYPE_CLONE;
    msg->u.clone.slot = slot;
    msg->u.clone.eip = eip;
    msg->u.clone.esp = esp;
}

void msg_reply(msg_t* msg, int retcode)
{
    msg->len = sizeof(msg->u.reply)+4;
    msg->type = MSGTYPE_REPLY;
    msg->u.reply.retcode = retcode;
}

void msg_ipc(msg_t* msg, DWORD slot, DWORD code, DWORD len, BYTE* data)
{
    msg->len = 20;
    msg->type = MSGTYPE_IPC;
    msg->u.ipc.to_slot = slot;
    msg->u.ipc.code = code;
    msg->u.ipc.len = len;
    if(len!=0 && data!=NULL)
    {
        msg->len += len;
        memcpy(msg->u.ipc.data, data, len);
    }
}

void msg_ipc_event(msg_t* msg, DWORD slot, DWORD type, DWORD param)
{
    msg->len = sizeof(msg->u.ipc_event)+4;
    msg->type = MSGTYPE_IPC;
    msg->u.ipc_event.to_slot = slot;
    msg->u.ipc_event.code = IPCCODE_EVENT;
    msg->u.ipc_event.type = type;
    msg->u.ipc_event.param = param;
}

void msg_focus_in(msg_t* msg, DWORD slot)
{
    msg->len = sizeof(msg->u.focus)+4;
    msg->type = MSGTYPE_GOT_FOCUS;
    msg->u.focus.slot = slot;
}

void msg_load_skin(msg_t* msg, char* fname)
{
    msg->len = 4;
    msg->type = MSGTYPE_LOAD_SKIN;
    if(fname)
    {
        msg->len += strlen(fname)+3;
        strcpy(msg->u.run.buf, fname);
    }
}

void k_focus_in(k_context* ctx)
{
    msg_t msg;
    msg_focus_in(&msg, ctx-g_slot);
    write_msg(ipc_server, &msg);
}
