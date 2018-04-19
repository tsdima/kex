#include "k_mem.h"
#include "k_event.h"
#include "k_net.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>

typedef struct
{
    const char* id;
    const char* fmt;
    void* data;
    int ofs[9];
} k_nmcli_line;

BYTE k_if_hwaddr[10];
DWORD k_if_ip[3];
DWORD k_if_dns[2];

const k_nmcli_line nmcli_lines[] = {
    {"GENERAL.HWADDR", "%x:%x:%x:%x:%x:%x", k_if_hwaddr, {0,1,2,3,4,5}},
    {"GENERAL.АППАРАТНЫЙ АДРЕС", "%x:%x:%x:%x:%x:%x", k_if_hwaddr, {0,1,2,3,4,5}},
    {"IP4.ADDRESS[1]", " ip = %d.%d.%d.%d/%d, gw = %d.%d.%d.%d", k_if_ip, {0,1,2,3,8,4,5,6,7}},
    {"IP4.АДРЕС[1]", " ip = %d.%d.%d.%d/%d, gw = %d.%d.%d.%d", k_if_ip, {0,1,2,3,8,4,5,6,7}},
    {"IP4.ADDRESS[1]", "%d.%d.%d.%d/%d", k_if_ip, {0,1,2,3,8}},
    {"IP4.АДРЕС[1]", "%d.%d.%d.%d/%d", k_if_ip, {0,1,2,3,8}},
    {"IP4.GATEWAY", "%d.%d.%d.%d", k_if_ip, {4,5,6,7}},
    {"IP4.ШЛЮЗ", "%d.%d.%d.%d", k_if_ip, {4,5,6,7}},
    {"IP4.DNS[1]", "%d.%d.%d.%d", k_if_dns, {0,1,2,3}},
    {NULL}
};

#define NMP(n) nm->ofs[n]+(BYTE*)nm->data

int k_nmcli_call(const char* cmd)
{
    char line[512],*p; const k_nmcli_line* nm;
    int ret = 1; FILE* fp = popen(cmd, "r"); if(!fp) return 1;
    while(fgets(line, sizeof(line), fp))
    {
        p = strchr(line,':'); if(!p) continue; else *p++ = 0;
        for(nm = nmcli_lines; nm->id; ++nm)
        {
            if(strcmp(line, nm->id)==0 && sscanf(p, nm->fmt, NMP(0), NMP(1), NMP(2), NMP(3), NMP(4), NMP(5), NMP(6), NMP(7), NMP(8))>0)
            {
                ret = 0; break;
            }
        }
    }
    fclose(fp);
    return ret;
}

k_timespec k_net_update_timeout;

void k_net_update()
{
    k_timespec now; k_time_get(&now);
    if(k_time_gt(&k_net_update_timeout,&now)) return;
    k_net_update_timeout = now; k_net_update_timeout.tv_sec++;
    KERNEL_MEM* km = kernel_mem();
    if(km->if_count==0)
    {
        if(k_nmcli_call("nmcli d show 2>/dev/null")) k_nmcli_call("nmcli d list 2>/dev/null");
        struct ifaddrs *ifap = NULL,*p; getifaddrs(&ifap);
        for(p=ifap; p!=NULL; p=p->ifa_next) if(p->ifa_addr!=NULL && p->ifa_addr->sa_family==AF_INET)
        {
            struct sockaddr_in ip,mask; DWORD i = km->if_count++;
            ip = *(struct sockaddr_in*)p->ifa_addr;
            mask = *(struct sockaddr_in*)p->ifa_netmask;
            strcpy(km->if_name[i], p->ifa_name);
            km->if_ip[i] = ip.sin_addr.s_addr;
            km->if_mask[i] = mask.sin_addr.s_addr;
            if(ip.sin_addr.s_addr==k_if_ip[0])
            {
                km->if_mac_hi[i] = *(WORD*)k_if_hwaddr;
                km->if_mac_lo[i] = *(DWORD*)(k_if_hwaddr+2);
                km->if_gateway[i] = k_if_ip[1];
                km->if_dns[i] = k_if_dns[0];
            }
        }
        if(ifap) freeifaddrs(ifap);
    }
}

DWORD k_net_info(k_context* ctx, BYTE devNo, BYTE func, DWORD* ebx, DWORD* ecx)
{
    k_net_update();
    KERNEL_MEM* km = kernel_mem(); if(func!=255 && devNo>=km->if_count) return -1;
    switch(func)
    {
    case 0: return devNo==0 ? 0 : 1; // type
    case 1: strcpy(user_mem(*ecx), km->if_name[devNo]); return 0;
    case 2: return 0; // reset
    case 3: return 0; // stop
    case 6: return 0; // send pck
    case 7: return 0; // recv pck
    case 8: *ebx = 0; return 0; // send bytes
    case 9: *ebx = 0; return 0; // recv bytes
    case 10: return 10; // link type
    case 255: return km->if_count; // iface count
    default: return -1;
    }
}

void ks_replace_socket(k_context* ctx, int cmp, int sock)
{
    int i; int* list = ctx->sockets;
    for(i=0; i<MAX_SOCKET; ++i) if(list[i]==cmp) { list[i] = sock; break; }
}

DWORD k_net_socket(k_context* ctx, BYTE func, DWORD* ebx, DWORD ecx, DWORD edx, DWORD esi, DWORD edi)
{
    k_net_update();
    DWORD ret = -1, err = 11, *p; int pair[2];
    switch(func)
    {
    case 0: ret = socket(ecx,edx,esi); if(ret!=-1) ks_replace_socket(ctx, 0, ret); break;
    case 1: ret = close(ecx); ks_replace_socket(ctx, ecx, 0); break;
    case 2: ret = bind(ecx, user_mem(edx), esi); break;
    case 3: ret = listen(ecx, 5); break;
    case 4: ret = connect(ecx, user_mem(edx), esi); break;
    case 5: ret = accept(ecx, user_mem(edx), &esi); break;
    case 6: ret = send(ecx, user_mem(edx), esi, edi); break;
    case 7: ret = recv(ecx, user_mem(edx), esi, edi); if(ret==-1 && (edi&MSG_DONTWAIT)!=0) err=6; break;
    case 8: p = user_pd(edx); ret = getsockopt(ecx, p[0], p[1], p+3, p+2); break;
    case 9: p = user_pd(edx); ret = setsockopt(ecx, p[0], p[1], p+3, p[2]); break;
    case 10: ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, pair); if(ret!=-1) { ret = pair[0]; *ebx = pair[1]; } break;
    }
    if(ret==-1) *ebx = err;
    return ret;
}

DWORD kp_ethernet(k_context* ctx, BYTE devNo, BYTE func, DWORD *ebx)
{
    KERNEL_MEM* km = kernel_mem(); if(devNo>=km->if_count) return -1;
    switch(func)
    {
    case 0: *ebx = km->if_mac_hi[devNo]; return km->if_mac_lo[devNo];
    default: return -1;
    }
}

DWORD kp_ipv4(k_context* ctx, BYTE devNo, BYTE func, DWORD ecx)
{
    KERNEL_MEM* km = kernel_mem(); if(devNo>=km->if_count) return -1;
    switch(func)
    {
    case 2: return km->if_ip[devNo];
    case 3: km->if_ip[devNo] = ecx; return 0;
    case 4: return km->if_dns[devNo];
    case 5: km->if_dns[devNo] = ecx; return 0;
    case 6: return km->if_mask[devNo];
    case 7: km->if_mask[devNo] = ecx; return 0;
    case 8: return km->if_gateway[devNo];
    case 9: km->if_gateway[devNo] = ecx; return 0;
    default: return 0;
    }
}

DWORD kp_icmp(k_context* ctx, BYTE devNo, BYTE func)
{
    return 0;
}

DWORD kp_udp(k_context* ctx, BYTE devNo, BYTE func)
{
    return 0;
}

DWORD kp_tcp(k_context* ctx, BYTE devNo, BYTE func)
{
    return 0;
}

DWORD kp_arp(k_context* ctx, BYTE devNo, BYTE func)
{
    switch(func)
    {
    case 0: return 0; // send pck
    case 1: return 0; // recv pck
    case 2: return 0; // # ARP recs
    case 3: return -1; // read ARP rec
    case 4: return 0; // add static rec
    case 5: return 0; // remove rec
    case 6: return 0; // send ARP announce
    case 7: return 0; // # conflicts
    default: return -1;
    }
}

DWORD k_net_proto(k_context* ctx, WORD proto, BYTE devNo, BYTE func, DWORD* ebx, DWORD* ecx)
{
    k_net_update();
    switch(proto)
    {
    case 0: return kp_ethernet(ctx, devNo, func, ebx);
    case 1: return kp_ipv4(ctx, devNo, func, *ecx);
    case 2: return kp_icmp(ctx, devNo, func);
    case 3: return kp_udp(ctx, devNo, func);
    case 4: return kp_tcp(ctx, devNo, func);
    case 5: return kp_arp(ctx, devNo, func);
    default: return -1;
    }
}
