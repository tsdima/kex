#include "k_mem.h"
#include "k_net.h"

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

DWORD k_net_info(k_context* ctx, BYTE devNo, BYTE func, DWORD* ebx, DWORD* ecx)
{
    switch(func)
    {
    case 0: return devNo==0 ? 1 : 0; // type
    case 1: strcpy(user_mem(*ecx), "NE2000"); return 0;
    case 2: return 0; // reset
    case 3: return 0; // stop
    case 6: return 0; // send pck
    case 7: return 0; // recv pck
    case 8: *ebx = 0; return 0; // send bytes
    case 9: *ebx = 0; return 0; // recv bytes
    case 10: return 10; // link type
    case 255: return 1; // iface count
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
    case 7: ret = recv(ecx, user_mem(edx), esi, edi); break;
    case 8: p = user_pd(edx); ret = getsockopt(ecx, p[0], p[1], p+3, p+2); break;
    case 9: p = user_pd(edx); ret = setsockopt(ecx, p[0], p[1], p+3, p[2]); break;
    case 10: ret = socketpair(AF_LOCAL, SOCK_STREAM, 0, pair); if(ret!=-1) { ret = pair[0]; *ebx = pair[1]; } break;
    }
    if(ret==-1) *ebx = err;
    return ret;
}

DWORD kp_ethernet(k_context* ctx, BYTE devNo, BYTE func, DWORD *ebx)
{
    switch(func)
    {
    case 0: *ebx = 0x0201; return 0x06050403; // get MAC addr
    default: return -1;
    }
}

DWORD kp_ipv4(k_context* ctx, BYTE devNo, BYTE func)
{
    switch(func)
    {
    case 2: return 0x73A1A8C0; // read IP
    case 4: return 0xFAA1A8C0; // read DNS
    case 6: return 0x00FFFFFF; // read Subnet
    case 8: return 0xFAA1A8C0; // read Gateway
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
    switch(proto)
    {
    case 0: return kp_ethernet(ctx, devNo, func, ebx);
    case 1: return kp_ipv4(ctx, devNo, func);
    case 2: return kp_icmp(ctx, devNo, func);
    case 3: return kp_udp(ctx, devNo, func);
    case 4: return kp_tcp(ctx, devNo, func);
    case 5: return kp_arp(ctx, devNo, func);
    default: return -1;
    }
}
