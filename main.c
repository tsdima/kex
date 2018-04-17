#include "k_mem.h"
#include "k_event.h"
#include "k_iconv.h"
#include "k_gui.h"
#include "k_syscall.h"
#include "k_proc.h"
#include "k_file.h"
#include "k_ipc.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>

#define SOCK_FILE "/tmp/kolibri.sock"

int fdlist[MAX_CHILD],fcount,appcount=0;
int winpos[MAX_CHILD];

k_bitmap_font font9 = {6,9};
k_bitmap_font font16 = {8,16};

void load_bitmap_font(char* name, k_bitmap_font* bf)
{
    FILE* fp = fopen(name, "rb"); if(fp==NULL) return;
    fseek(fp, 0, SEEK_END); DWORD len = ftell(fp); fseek(fp, 0, SEEK_SET);
    bf->bmp = malloc(len); fread(bf->bmp, 1, len, fp); fclose(fp);
    bf->chars = len/bf->height;
}

int find_free(int* list, int max)
{
    int i; for(i=1; i<max; ++i) if(list[i]<=0) return i;
    return -1;
}

void move_to_top(int slot)
{
    int i;
    for(i=1; i<MAX_CHILD-1 && winpos[i]>0; ++i) if(winpos[i]==slot)
    {
        for(; i<MAX_CHILD-1 && winpos[i+1]>0; ++i) winpos[i] = winpos[i+1];
        break;
    }
    winpos[i] = kernel_mem()->active_slot = slot;
}

void update_zpos()
{
    k_context* slot = g_slot;
    int i,j,max = k_max_slot();
    for(i=j=1; i<MAX_CHILD && winpos[i]>0; ++i) if(winpos[i]<=max) winpos[j++] = winpos[i];
    for(; j<MAX_CHILD && winpos[j]>0; ++j) winpos[j] = 0;
    for(i=1; i<MAX_CHILD && winpos[i]>0; ++i)
    {
        slot[i].window_zpos = winpos[i];
        slot[winpos[i]].window_zpos_me = i;
    }
}

void do_run(msg_t* msg, int fd)
{
    int newslot = find_free(fdlist, MAX_CHILD), pid = -1;
    if(newslot>0)
    {
        move_to_top(newslot);
        int pair[2]; socketpair(AF_LOCAL, SOCK_STREAM, 0, pair);
        pid = fork();
        if(pid==0)
        { // child
            msg_t rply; msg_reply(&rply, getpid()-base_pid);
            close(pair[1]); ipc_server = pair[0];
            if(msg->type==MSGTYPE_RUN)
            {
                int shmid = find_free(kernel_mem()->shmtc, MAX_CHILD);
                k_mem_init(shmid);
                k_gui_init();
                k_syscall_init();
                k_set_skin(k_skin_open());
                k_set_slot(newslot, shmid, msg->u.run.buf);
                if(fd>0) write_msg(fd, &rply);
                k_exec(g_slot+newslot, msg->u.run.buf, msg->u.run.args==0?NULL:msg->u.run.buf+msg->u.run.args);
            }
            else // MSGTYPE_CLONE
            {
                k_mem_init(g_slot[msg->u.clone.slot].shmid);
                k_mem_reopen();
                k_gui_init();
                k_syscall_init();
                k_set_skin(k_skin_open());
                k_set_slot_from(newslot, msg->u.clone.slot);
                if(fd>0) write_msg(fd, &rply);
                k_start_thread(msg->u.clone.eip, msg->u.clone.esp);
            }
            exit(-1);
        }
        else if(pid>0)
        { // self
            close(pair[0]);
            fdlist[newslot] = pair[1]; ++appcount;
            if(pair[1]>=fcount) fcount = pair[1]+1;
        }
    }
    if (pid<0)
    {
        msg_reply(msg, -1);
        if(fd>0) write_msg(fd, msg);
    }
}

void do_ipc_msg(msg_t* msg, int from)
{
    msg->u.ipc.from_slot = from;
    int dst = msg->u.ipc.to_slot;
    if (g_slot[dst].tid!=0)
    {
        write_msg(fdlist[dst], msg);
    }
    else
    {
        msg_ipc(msg, from, 4, 0, NULL);
        write_msg(fdlist[from], msg);
    }
}

void do_focus(msg_t* msg)
{
    move_to_top(msg->u.focus.slot);
    update_zpos();
}

void remove_from_window_stack(int slot)
{
    k_context* ctx = g_slot+slot;
    k_usm_clean(ctx);
    if(--kernel_mem()->shmtc[ctx->shmid]<=0) k_mem_done(ctx->shmid);
    memset(ctx, 0, sizeof(*ctx));
    update_zpos();
}

void do_load_skin(msg_t* msg)
{
    k_load(NULL,(BYTE*)msg->u.run.buf,0,k_skin_alloc,NULL,NULL);
    int i; msg_load_skin(msg,NULL);
    for(i=1; i<MAX_CHILD && fdlist[i]; ++i) if(fdlist[i]>0) write_msg(fdlist[i], msg);
}

int main(int argc, char **argv)
{
    if(argc<2)
    {
        printf("\nUse: %s kolibriapp args\n", argv[0]);
        return -1;
    }

    sprintf(k_root, "%s/.kex/root/", getenv("HOME")); base_pid = getpid();

    char* p = strchr(k_root,0);
    strcpy(p, "../char.mt"); load_bitmap_font(k_root, &font9);
    strcpy(p, "../charUni.mt"); load_bitmap_font(k_root, &font16); *p = 0;

    struct sockaddr_un saddr; socklen_t addrlen;
    fd_set set; msg_t msg;

    saddr.sun_family = AF_LOCAL;
    strcpy(saddr.sun_path, SOCK_FILE);
    addrlen = sizeof(struct sockaddr_in);
    memset(fdlist,0,sizeof(fdlist));
    memset(winpos,0,sizeof(winpos));

    msg_run(&msg, argv[1], argc<3?NULL:argv[2]);

    int svr = socket(AF_LOCAL, SOCK_STREAM, 0),ok,i;
    if(connect(svr, (struct sockaddr*)&saddr, sizeof(saddr))==0)
    {
        write_msg(svr, &msg);
        close(svr);
        return 0;
    }
    unlink(SOCK_FILE);
    if(bind(svr, (struct sockaddr*)&saddr, sizeof(saddr))<0) return -1;
    listen(svr, 5); fdlist[0] = svr; fcount=svr+1;

    k_kernel_mem_init();

    DWORD width,height;
    k_gui_init();
    k_get_screen_size(&width, &height);
    k_set_desktop_rect(0, 0, width-1, height-1);

    k_load(NULL,(BYTE*)"DEFAULT.SKN",0,k_skin_alloc,NULL,NULL);

    do_run(&msg, 0); if(appcount==0) return 0;

    for(ok=1; ok; )
    {
        FD_ZERO(&set);
        for(i=0; i<MAX_CHILD && fdlist[i]; ++i) if(fdlist[i]>0) FD_SET(fdlist[i], &set);
        int sel = select(fcount, &set, NULL, NULL, NULL); if(sel<0) break;
        for(i=0; i<MAX_CHILD && fdlist[i]; ++i) if(fdlist[i]>0 && FD_ISSET(fdlist[i], &set))
        {
            int fd = i>0 ? fdlist[i] : accept(svr, (struct sockaddr*)&saddr, &addrlen);
            if(fd>0)
            {
                if(read_msg(fd, &msg))
                {
                    switch(msg.type)
                    {
                    case MSGTYPE_RUN: case MSGTYPE_CLONE: do_run(&msg, fd); break;
                    case MSGTYPE_IPC: do_ipc_msg(&msg, i); break;
                    case MSGTYPE_GOT_FOCUS: do_focus(&msg); break;
                    case MSGTYPE_LOAD_SKIN: do_load_skin(&msg); break;
                    }
                }
                else if(i>0)
                {
                    remove_from_window_stack(i);
                    close(fd); fdlist[i] = -1;
                    if(--appcount<=0) ok = 0;
                    wait(&fd);
                }
                if(i==0) close(fd);
            }
        }
    }
    close(svr);
    unlink(SOCK_FILE);
    k_kernel_mem_cleanup();
    return 0;
}
