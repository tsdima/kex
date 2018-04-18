#include "k_mem.h"
#include "k_event.h"
#include "k_iconv.h"
#include "k_proc.h"
#include "k_gui.h"
#include "k_file.h"
#include "k_ipc.h"
#include "k_net.h"
#include "k_syscall.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ucontext.h>

int base_pid = 0;

int gettid()
{
    return getpid()-base_pid;
}

void k_debug_put(BYTE ch)
{
    KERNEL_MEM* km = kernel_mem();
    km->debug_board[km->debug_in_pos++] = ch;
    km->debug_in_pos %= DEBUG_BOARD_LEN;
}

DWORD k_debug_get(DWORD* ebx)
{
    DWORD ret; KERNEL_MEM* km = kernel_mem();
    if(km->debug_in_pos==km->debug_out_pos) return *ebx = 0;
    ret = km->debug_board[km->debug_out_pos++]; *ebx = 1;
    km->debug_out_pos %= DEBUG_BOARD_LEN;
    return ret;
}

void k_putc866(char ch)
{
    char out[4]; k_strncpy((BYTE*)out,3,(BYTE*)&ch,0,1);
    fputs(out, stderr);
}

void k_vprintf(int opt, char* fmt, va_list va)
{
    char buf[4096],*s; int nl=1;
    vsprintf(buf, fmt, va);
    for(s=buf; *s; ++s)
    {
        if(nl)
        {
            k_debug_put('K'); k_debug_put(' ');
            k_debug_put(':'); k_debug_put(' ');
        }
        if((opt&1)!=0) k_putc866(*s);
        k_debug_put(*s);
        nl = (*s == 10);
    }
}

void k_debug_printf(char* fmt, ...)
{
    va_list va; va_start(va, fmt); k_vprintf(0, fmt, va);
}

void k_printf(char* fmt, ...)
{
    va_list va; va_start(va, fmt); k_vprintf(1, fmt, va);
}

void k_panic(char* msg)
{
    k_printf("%s\n", msg);
    exit(-1);
}

void k_get_slot_info(k_context* ctx, K_SLOT_INFO* info, int slot)
{
    if(slot<-1 || slot>=MAX_CHILD) { info->state = 9; return; }
    if(slot!=-1) ctx = g_slot+slot;
    info->cputime = 0;
    info->zpos_me = ctx->window_zpos_me;
    info->zpos = ctx->window_zpos;
    memcpy(info->name, ctx->name, sizeof(ctx->name));
    info->base = 0;
    info->last = ctx->memsize-1;
    info->pid = ctx->tid;
    info->window_x = ctx->window_x;
    info->window_y = ctx->window_y;
    info->window_w = ctx->window_w>0?ctx->window_w-1:0;
    info->window_h = ctx->window_h>0?ctx->window_h-1:0;
    info->state = ctx->tid!=0 ? 0 : 9;
    k_get_client_rect(ctx, &info->client_x, &info->client_y, &info->client_w, &info->client_h);
    info->window_state = ctx->window_state;
    info->event_mask = ctx->event_mask;
    info->kbd_mode = ctx->kbd_mode;
}

void k_set_slot(DWORD slot, int shmid, char* name)
{
    k_context* ctx = g_slot+slot;
    char* p = strrchr(name,'/'); if(p) name = p+1;

    memset(ctx, 0, sizeof(k_context));
    ctx->pid = getpid();
    ctx->tid = gettid();
    ctx->shmid = shmid;
    ctx->event_mask = 7;
    ctx->event_pending = K_EVMASK_REDRAW;
    strncpy(ctx->name, name, sizeof(ctx->name)-1);
    p = strchr(ctx->name, '.'); if(p) *p = 0;
    k_copy_path(ctx->curpath, (BYTE*)"/sys/");
    kernel_mem()->shmtc[shmid] = 1;
}

void k_set_slot_from(DWORD slot, DWORD from)
{
    k_context* ctx = g_slot+slot;
    k_context* src = g_slot+from;

    memset(ctx, 0, sizeof(k_context));
    ctx->pid = getpid();
    ctx->tid = gettid();
    ctx->shmid = src->shmid;
    ctx->memsize = src->memsize;
    ctx->event_mask = src->event_mask;
    ctx->event_pending = K_EVMASK_REDRAW;
    ctx->kbd_mode = src->kbd_mode;
    strncpy(ctx->name, src->name, sizeof(ctx->name)-1);
    k_copy_path(ctx->curpath, src->curpath);
    kernel_mem()->shmtc[src->shmid]++;
}

DWORD k_get_slot()
{
    k_context* slot = g_slot;
    DWORD i,pid=getpid(),tid=gettid();
    for(i=1; i<MAX_CHILD; ++i) if(slot[i].pid == pid && slot[i].tid == tid) return i;
    return 0;
}

DWORD k_find_slot(DWORD tid)
{
    DWORD i; k_context* slot = g_slot;
    for(i=1; i<MAX_CHILD; ++i) if(slot[i].tid == tid) return i;
    return 0;
}

DWORD k_get_slot_by_point(int x, int y)
{
    DWORD i,best=0; k_context* slot = g_slot;
    for(i=1; i<MAX_CHILD; ++i)
        if(slot[i].tid != 0 && x >= slot[i].window_x && y >= slot[i].window_y &&
            x < slot[i].window_x+slot[i].window_w && y < slot[i].window_y+slot[i].window_h &&
            (best==0 || slot[best].window_zpos_me<slot[i].window_zpos_me))
                best = i;
    return best;
}

DWORD k_max_slot()
{
    DWORD i; k_context* slot = g_slot;
    for(i=MAX_CHILD-1; i>0 && slot[i].pid == 0 && slot[i].tid == 0; --i);
    return i;
}

void k_kill_by_slot(DWORD slot)
{
    if(slot<1||slot>=MAX_CHILD) return;
    k_context* ctx = g_slot+slot;
    if(ctx->tid==0) return;
    kill(ctx->pid, SIGKILL);
}

void k_update_memusage(k_context* ctx)
{
    ctx->memsize = k_mem_get_size();
}

DWORD k_new_thread(k_context* ctx, DWORD slot, DWORD eip, DWORD esp)
{
    msg_t msg;
    msg_clone(&msg, slot, eip, esp);
    write_msg(ipc_server, &msg);
    for(ctx->retcode = 0; ctx->retcode==0;) k_process_ipc_event(ctx, &msg);
    return ctx->retcode;
}

DWORD k_send_ipc_message(k_context* ctx, DWORD tid, BYTE* data, DWORD len)
{
    int slot = k_find_slot(tid); if(slot<=0) return 4;
    msg_t msg; msg_ipc(&msg, slot, IPCCODE_DATA, len, NULL);
    write_msg(ipc_server, &msg);
    do k_process_ipc_event(ctx, &msg); while(msg.type!=MSGTYPE_IPC || msg.u.ipc.code>=IPCCODE_DATA);
    if(msg.u.ipc.code!=0) return msg.u.ipc.code;
    while(len>0)
    {
        DWORD n = len<4000 ? len : 4000;
        msg_ipc(&msg, slot, n==len?0:IPCCODE_DATA, n, data);
        write_msg(ipc_server, &msg);
        len -= n; data += n;
    }
    do k_process_ipc_event(ctx, &msg); while(msg.type!=MSGTYPE_IPC || msg.u.ipc.code>=IPCCODE_DATA);
    return 0;
}

void k_make_active(DWORD slot)
{
    msg_t msg; msg_ipc(&msg, slot, IPCCODE_ACTIVATE, 0, NULL);
    write_msg(ipc_server, &msg);
    kernel_mem()->active_slot = slot;
}

DWORD k_send_event(DWORD type, DWORD param)
{
    msg_t msg; msg_ipc_event(&msg, kernel_mem()->active_slot, type, param);
    write_msg(ipc_server, &msg);
    return 0;
}

#define R_FL     gregs[REG_EFL]
#define R_ERR    gregs[REG_ERR]
#define R_TRAPNO gregs[REG_TRAPNO]

#ifdef __x86_64__

#define R_AX gregs[REG_RAX]
#define R_BX gregs[REG_RBX]
#define R_CX gregs[REG_RCX]
#define R_DX gregs[REG_RDX]
#define R_SI gregs[REG_RSI]
#define R_DI gregs[REG_RDI]
#define R_BP gregs[REG_RBP]
#define R_SP gregs[REG_RSP]
#define R_IP gregs[REG_RIP]
#define R_CS ((WORD)gregs[REG_CSGSFS])

#define FMT1 "%08llx"
#define FMT2 "%016llx"
#define FMTD "%lld"

#else

#define R_AX gregs[REG_EAX]
#define R_BX gregs[REG_EBX]
#define R_CX gregs[REG_ECX]
#define R_DX gregs[REG_EDX]
#define R_SI gregs[REG_ESI]
#define R_DI gregs[REG_EDI]
#define R_BP gregs[REG_EBP]
#define R_SP gregs[REG_ESP]
#define R_IP gregs[REG_EIP]
#define R_CS gregs[REG_CS]

#define FMT1 "%08x"
#define FMT2 "%08x"
#define FMTD "%d"

#endif

void OnSigSegv(int sig, siginfo_t* info, void* extra)
{
#ifdef __x86_64__
    k_load_fsbase();
#endif
    greg_t* gregs = ((struct ucontext*)extra)->uc_mcontext.gregs; DWORD slot = k_get_slot();
    DWORD* eax = (DWORD*)&R_AX;
    DWORD* ebx = (DWORD*)&R_BX;
    DWORD* ecx = (DWORD*)&R_CX;
    DWORD* edx = (DWORD*)&R_DX;
    DWORD* esi = (DWORD*)&R_SI;
    DWORD* edi = (DWORD*)&R_DI;
    DWORD* ebp = (DWORD*)&R_BP;
    if(R_TRAPNO==13 && R_CS == 0x0f && slot!=0)
    {
        WORD cmd = k_get_word(R_IP);
        if(cmd==0x40CD)
        {
            k_timespec now,timeout; DWORD x,y; QWORD q; int err=0; KERNEL_MEM* km = kernel_mem();
            k_context* ctx = g_slot+slot; DWORD f_nr = *eax;
            switch(*eax)
            {
            case -1:
                exit(0);
                break;
            case 0:
                k_window(ctx, *ebx>>16, *ecx>>16, *ebx&0xFFFF, *ecx&0xFFFF, *edx, *edi);
                break;
            case 1:
                k_draw_pixel(ctx, *ebx, *ecx, *edx);
                break;
            case 2:
                *eax = k_get_key(ctx);
                break;
            case 3:
                *eax = k_bcd_time();
                break;
            case 4:
                k_draw_text(ctx, *ebx>>16, *ebx&0xFFFF, user_pb(*edx), *esi, *ecx, *edi);
                break;
            case 5:
                k_time_get(&timeout);
                k_time_add_ms(&timeout, *ebx*10);
                for(;;)
                {
                    k_time_get(&now);
                    if(k_time_gt(&now, &timeout)) break;
                    k_process_event(ctx);
                }
                break;
            case 7:
                k_draw_image(ctx, *edx>>16, *edx&0xFFFF, *ecx>>16, *ecx&0xFFFF, user_pb(*ebx), 24, 0, NULL);
                break;
            case 8:
                if (*edx>>31)
                {
                    k_remove_button(ctx, *edx);
                }
                else
                {
                    k_define_button(ctx, (*ebx>>16)+ctx->client_x, (*ecx>>16)+ctx->client_y, *ebx&0xFFFF, *ecx&0xFFFF, *edx);
                    if((*edx&KBS_NO_DRAW)==0) k_draw_button(ctx, *ebx>>16, *ecx>>16, *ebx&0xFFFF, *ecx&0xFFFF, *esi);
                }
                break;
            case 9:
                k_get_slot_info(ctx, (K_SLOT_INFO*)user_mem(*ebx), *ecx);
                *eax = k_max_slot();
                break;
            case 10:
                k_process_event(ctx); *eax = k_check_event(ctx);
                while(*eax == 0) { k_process_event(ctx); *eax = k_check_event(ctx); }
                break;
            case 11:
                k_process_event(ctx); *eax = k_check_event(ctx);
                break;
            case 12:
                switch(*ebx)
                {
                case 1: k_clear_buttons(ctx); break;
                case 2: k_clear_redraw(ctx); break;
                default: err = 1; break;
                }
                break;
            case 13:
                k_draw_rect(ctx, *ebx>>16, *ecx>>16, *ebx&0xFFFF, *ecx&0xFFFF, *edx);
                break;
            case 14:
                k_get_screen_size(&x,&y); *eax = ((x-1)<<16)+y-1;
                break;
            case 17:
                *eax = k_get_button(ctx);
                break;
            case 18:
                switch(*ebx)
                {
                case 2: k_kill_by_slot(*ecx); break;
                case 3: k_make_active(*ecx); break;
                case 4: *eax = 1<<30; break; // CPU idle clocks
                case 5: *eax = 1<<30; break; // CPU clock
                case 7: *eax = km->active_slot; break;
                case 14: *eax = 0; break; // TODO: wait VRTC
                case 16: *eax = 1<<19; break; // RAM size free KB
                case 17: *eax = 1<<20; break; // RAM size total KB
                case 18: x = k_find_slot(*ecx); if(x==0) *eax=-1; else { *eax=0; k_kill_by_slot(x); } break;
                case 19: // mouse settings
                    switch(*ecx)
                    {
                    case 4: k_move_mouse(*edx>>16, *edx&0xFFFF); break;
                    case 6: *eax = km->mouse_dbl_click_delay; break;
                    case 7: km->mouse_dbl_click_delay = *edx&255; break;
                    default: err = 1; break;
                    }
                    break;
                case 21: *eax = k_find_slot(*ecx); break;
                default: err = 1; break;
                }
                break;
            case 21:
                switch(*ebx)
                {
                case 2: *eax = k_set_keyboard_layout(*ecx, *edx); break;
                case 5: *eax = k_set_keyboard_lang(*ecx); break;
                case 12: *eax = 0; km->pci_enabled = *ecx; break;
                default: err = 1; break;
                }
                break;
            case 23:
                k_time_get(&timeout);
                k_time_add_ms(&timeout, *ebx*10);
                k_process_event(ctx); *eax = k_check_event(ctx);
                while(*eax == 0)
                {
                    k_time_get(&now);
                    if(k_time_gt(&now, &timeout)) break;
                    k_process_event(ctx); *eax = k_check_event(ctx);
                }
                break;
            case 26:
                switch(*ebx)
                {
                case 2: *eax = k_get_keyboard_layout(*ecx, *edx); break;
                case 5: *eax = k_get_keyboard_lang(); break;
                case 9: k_time_get(&now); *eax = now.tv_sec*100+now.tv_nsec/10000000; break;
                case 10: k_time_get(&now); q = now.tv_sec*1000000000L+now.tv_nsec; *edx=q>>32; *eax=q; break;
                case 11: *eax = 0; break; // no lowlevel HD access
                case 12: *eax = km->pci_enabled; break;
                default: err = 1; break;
                }
                break;
            case 29:
                *eax = k_bcd_date();
                break;
            case 30:
                switch(*ebx)
                {
                case 1: *eax = k_set_curpath(ctx, user_mem(*ecx), -1); break;
                case 2: *eax = k_get_curpath(ctx, user_mem(*ecx), -1, *edx); break;
                case 3: *eax = k_set_extfs(user_mem(*ecx)); break;
                case 4: *eax = k_set_curpath(ctx, user_mem(*ecx), *edx); break;
                case 5: *eax = k_get_curpath(ctx, user_mem(*ecx), *esi, *edx); break;
                default: err = 1; break;
                }
                break;
            case 34:
                *eax = k_get_slot_by_point(*ebx,*ecx);
                break;
            case 35:
                k_get_screen_size(&x, &y);
                *eax = k_get_pixel(ctx, *ebx%x, *ebx/x);
                break;
            case 36:
                k_get_image(ctx, *edx>>16, *edx&0xFFFF, *ecx>>16, *ecx&0xFFFF, user_pb(*ebx));
                break;
            case 37:
                switch(*ebx)
                {
                case 0: *eax = k_get_mouse(0,0); break;
                case 1: *eax = k_get_mousepos(ctx,0,0); break;
                case 2: *eax = k_get_mousestate(ctx)&0xFF; break;
                case 3: *eax = k_get_mousestate(ctx); break;
                case 4: *eax = k_cursor_load(ctx, *ecx, *edx); break;
                case 5: *eax = k_cursor_set(ctx, *ecx); break;
                case 6: *eax = k_cursor_delete(ctx, *ecx); break;
                case 7: *eax = 0; break; // TODO: scroll data
                default: err = 1; break;
                }
                break;
            case 38:
                k_draw_line(ctx, *ebx>>16, *ecx>>16, *ebx&0xFFFF, *ecx&0xFFFF, *edx);
                break;
            case 40:
                *eax = ctx->event_mask; ctx->event_mask = *ebx;
                break;
            case 47:
                k_draw_num(ctx, *edx>>16, *edx&0xFFFF, *esi, *ebx, (*ebx&1)==0?ecx:user_mem(*ecx), *edi);
                break;
            case 48:
                switch(*ebx)
                {
                case 3: k_get_skin_colors(user_pb(*ecx), *edx); break;
                case 4: *eax = k_get_skin_height(); break;
                case 5: k_get_desktop_rect(&x,&y,eax,ebx); *eax |= x<<16; *ebx |= y<<16; break;
                case 6: k_set_desktop_rect(*ecx>>16, *edx>>16, *ecx&0xFFFF, *edx&0xFFFF); break;
                case 8: *eax = k_load_skin(ctx, user_pb(*ecx)); break;
                default: err = 1; break;
                }
                break;
            case 51:
                switch(*ebx)
                {
                case 1: *eax = k_new_thread(ctx, slot, *ecx, *edx); break;
                default: err = 1; break;
                }
                break;
            case 54:
                switch(*ebx)
                {
                case 0: *eax = km->clipboard_count; break;
                case 1: *eax = k_clipboard_get(*ecx); break;
                case 2: *eax = k_clipboard_add(*ecx, *edx); break;
                case 3: *eax = k_clipboard_remove_last(); break;
                case 4: *eax = 0; break;
                default: err = 1; break;
                }
                break;
            case 60:
                switch(*ebx)
                {
                case 1: *eax = 0; ctx->ipc_buffer = user_pb(*ecx); ctx->ipc_buf_len = *edx; break;
                case 2: *eax = k_send_ipc_message(ctx, *ecx, user_pb(*edx), *esi); break;
                default: err = 1; break;
                }
                break;
            case 61:
                // TODO: set gs selector
                switch(*ebx)
                {
                case 1: k_get_screen_size(&x,&y); *eax=(x<<16)+y; break;
                default: err = 1; break;
                }
                break;
            case 62:
                if(km->pci_enabled==0) { *eax = -1; break; }
                switch(*ebx&255)
                {
                case 0: *eax = 0x100; break;
                case 1: *eax = k_pci_get_last_bus(); break;
                case 2: *eax = 1; break;
                case 4: case 5: case 6: *eax = k_pci_read_reg(*ebx,*ecx); break;
                default: *eax = -1; break;
                }
                break;
            case 63:
                switch(*ebx)
                {
                case 1: k_debug_put(*ecx); k_putc866(*ecx); break;
                case 2: *eax = k_debug_get(ebx); break;
                default: err = 1; break;
                }
                break;
            case 64:
                switch(*ebx)
                {
                case 1: *eax = k_mem_size(*ecx); k_update_memusage(ctx); break;
                default: err = 1; break;
                }
                break;
            case 65:
                k_draw_image(ctx, *edx>>16, *edx&0xFFFF, *ecx>>16, *ecx&0xFFFF, user_pb(*ebx), *esi, *ebp, user_pd(*edi));
                break;
            case 66:
                switch(*ebx)
                {
                case 1: ctx->kbd_mode = *ecx; break;
                case 2: *eax = ctx->kbd_mode; break;
                case 3: *eax = k_get_keyboard_modifiers(); break;
                default: err = 1; break;
                }
                break;
            case 67:
                k_move_size_window(ctx, *ebx, *ecx, *edx, *esi);
                break;
            case 68:
                switch(*ebx)
                {
                case 1: usleep(1); break;
                case 11: *eax = k_heap_init(); break;
                case 12: *eax = k_heap_alloc(*ecx); break;
                case 13: *eax = k_heap_free(*ecx); break;
                case 18: *eax = k_load_dll(ctx, user_pb(*ecx), *edx); break;
                case 19: *eax = k_load_dll(ctx, user_pb(*ecx), 0); break;
                case 20: *eax = k_heap_realloc(*edx, *ecx); break;
                case 22: *edx = k_usm_open(ctx, *ecx, *edx, *esi, eax); break;
                case 23: *eax = k_usm_close(ctx, *ecx); break;
                case 27: *eax = k_load_file(ctx, user_pb(*ecx), 0, edx); break;
                case 28: *eax = k_load_file(ctx, user_pb(*ecx), *edx, edx); break;
                default: err = 1; break;
                }
                k_update_memusage(ctx);
                break;
            case 70:
                err = k_file_syscall(ctx, eax, ebx, 0);
                break;
            case 71:
                k_set_title(ctx, *ecx, *ebx==1?-1:(*edx&0xFF));
                break;
            case 72:
                switch(*ebx)
                {
                case 1: *eax = k_send_event(*ecx, *edx); break;
                default: err = 1; break;
                }
                break;
            case 73:
                k_blit_image(ctx, *ebx, *ecx);
                break;
            case 74:
                *eax = k_net_info(ctx, *ebx>>8, *ebx, ebx, ecx);
                break;
            case 75:
                *eax = k_net_socket(ctx, *ebx, ebx, *ecx, *edx, *esi, *edi);
                break;
            case 76:
                *eax = k_net_proto(ctx, *ebx>>16, *ebx>>8, *ebx, ebx, ecx);
                break;
            case 80:
                err = k_file_syscall(ctx, eax, ebx, 1);
                break;
            default:
                err = 1;
                break;
            }
            if(err==1)
            {
                printf("%02X.%08X: mcall %d, 0x%X, 0x%X, 0x%X\n", slot, (DWORD)R_IP, f_nr, *ebx, *ecx, *edx);
            }
#ifdef __x86_64__
            R_IP = k_stub_resume(R_IP+2);
            k_set_fsbase();
#else
            R_IP += 2;
#endif
            return;
        }
    }
    if(R_CS == 0x0f && slot!=0)
    {
        DWORD* stk = user_pd(R_SP);
        k_debug_printf("Process - forced terminate PID: %08X [%.12s]\n", g_slot[slot].tid, g_slot[slot].name);
        k_debug_printf("EAX : %08X EBX : %08X ECX : %08X\n", *eax, *ebx, *ecx);
        k_debug_printf("EDX : %08X ESI : %08X EDI : %08X\n", *edx, *esi, *edi);
        k_debug_printf("EBP : %08X EIP : %08X ESP : %08X\n", *ebp, (DWORD)R_IP, (DWORD)R_SP);
        k_debug_printf("Flags : %08X CS: 0x%08X (application)\n", (DWORD)R_FL, R_CS);
        k_debug_printf("Stack dump:\n");
        k_debug_printf("[ESP+00]: %08X [ESP+04]: %08X [ESP+08]: %08X\n", stk[0], stk[1], stk[2]);
        k_debug_printf("[ESP+12]: %08X [ESP+16]: %08X [ESP+20]: %08X\n", stk[3], stk[4], stk[5]);
        k_debug_printf("[ESP+24]: %08X [ESP+28]: %08X [ESP+32]: %08X\n", stk[6], stk[7], stk[8]);
        k_debug_printf("destroy app object\n");
        printf("Process: %.12s\n", g_slot[slot].name);
    }
    printf("err : 0x"FMT1" trapno: "FMTD" addr: 0x"FMT2"\n", R_ERR, R_TRAPNO, (greg_t)info->si_addr);
    printf("EAX : 0x"FMT2" EBX : 0x"FMT2" ECX : 0x"FMT2"\n", R_AX, R_BX, R_CX);
    printf("EDX : 0x"FMT2" ESI : 0x"FMT2" EDI : 0x"FMT2"\n", R_DX, R_SI, R_DI);
    printf("EBP : 0x"FMT2" EIP : 0x"FMT2" ESP : 0x"FMT2"\n", R_BP, R_IP, R_SP);
    printf("Flags : 0x"FMT1" CS: 0x%04x\n", R_FL, R_CS);
    FILE* fp=fopen("core.kex","wb"); fwrite(user_mem(0), 1, k_mem_get_size(), fp); fclose(fp);
    exit(0);
};

#define stack_size 0x8000

void k_syscall_init()
{
#ifdef __x86_64__
    k_save_fsbase();
#endif

    struct sigaltstack altstack;
    struct sigaction sigsegv_action;

    altstack.ss_sp = malloc(stack_size);
    altstack.ss_flags = 0;
    altstack.ss_size = stack_size;
    sigaltstack(&altstack, NULL);

    sigsegv_action.sa_sigaction = OnSigSegv;
    sigemptyset(&sigsegv_action.sa_mask);
    sigsegv_action.sa_flags = SA_ONSTACK | SA_SIGINFO;
    sigaction(SIGSEGV, &sigsegv_action, NULL);
}
