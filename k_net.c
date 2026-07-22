#include "k_mem.h"
#include "k_event.h"
#include "k_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>

typedef struct
{
    const char* id;
    const char* fmt;
    int ofs[9];
} k_nmcli_line;

#define MAX_IF_COUNT 4
#define k_if_hwaddr(i) k_if_data[i]
#define k_if_ip(i) *(DWORD*)&k_if_data[i][10]
#define k_if_gw(i) *(DWORD*)&k_if_data[i][14]
#define k_if_dns(i) *(DWORD*)&k_if_data[i][22]

BYTE k_if_data[MAX_IF_COUNT][30];

const k_nmcli_line nmcli_lines[] = {
    {"GENERAL.HWADDR", "%x:%x:%x:%x:%x:%x", {0,1,2,3,4,5}},
    {"GENERAL.АППАРАТНЫЙ АДРЕС", "%x:%x:%x:%x:%x:%x", {0,1,2,3,4,5}},
    {"IP4.ADDRESS[1]", " ip = %d.%d.%d.%d/%d, gw = %d.%d.%d.%d", {10,11,12,13,18,14,15,16,17}},
    {"IP4.АДРЕС[1]", " ip = %d.%d.%d.%d/%d, gw = %d.%d.%d.%d", {10,11,12,13,18,14,15,16,17}},
    {"IP4.ADDRESS[1]", "%d.%d.%d.%d/%d", {10,11,12,13,18}},
    {"IP4.АДРЕС[1]", "%d.%d.%d.%d/%d", {10,11,12,13,18}},
    {"IP4.GATEWAY", "%d.%d.%d.%d", {14,15,16,17}},
    {"IP4.ШЛЮЗ", "%d.%d.%d.%d", {14,15,16,17}},
    {"IP4.DNS[1]", "%d.%d.%d.%d", {22,23,24,25}},
    {NULL}
};

#define NMP(n) nm->ofs[n]+k_if_data[i]

int k_nmcli_call(const char* cmd)
{
    char line[512],*p; const k_nmcli_line* nm;
    int i,ret = 1; FILE* fp = popen(cmd, "r"); if(!fp) return 1;
    for(i=-1; i<MAX_IF_COUNT && fgets(line, sizeof(line), fp);)
    {
        p = strchr(line,':'); if(!p) continue; else *p++ = 0;
        for(nm = nmcli_lines; nm->id; ++nm)
        {
            if(strcmp(line, nm->id)==0)
            {
                if(nm<nmcli_lines+2) if(++i>=MAX_IF_COUNT) break;
                if(sscanf(p, nm->fmt, NMP(0), NMP(1), NMP(2), NMP(3), NMP(4), NMP(5), NMP(6), NMP(7), NMP(8))>0)
                {
                    ret = 0; break;
                }
            }
        }
    }
    fclose(fp);
    return ret;
}

k_timespec k_net_update_timeout;

static int k_net_debug(void)
{
    static int cached = -1;
    if(cached < 0) { const char* e = getenv("KEX_NET_DEBUG"); cached = (e && *e && *e!='0'); }
    return cached;
}
#define NET_DBG(...) do { if(k_net_debug()) fprintf(stderr, __VA_ARGS__); } while(0)

// Return the first nameserver IP from /etc/resolv.conf, in network byte order,
// or 0 if none is listed. 127.0.0.53 (systemd-resolved) is a valid DNS.
DWORD k_read_resolv_conf_dns()
{
    FILE* fp = fopen("/etc/resolv.conf", "r"); if(!fp) return 0;
    char line[256]; DWORD ip = 0;
    while(fgets(line, sizeof(line), fp))
    {
        int a,b,c,d;
        if(sscanf(line, " nameserver %d.%d.%d.%d", &a,&b,&c,&d)==4)
        {
            ip = a | (b<<8) | (c<<16) | (d<<24);
            break;
        }
    }
    fclose(fp);
    return ip;
}

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
            struct sockaddr_in ip,mask; DWORD i = km->if_count++,j;
            ip = *(struct sockaddr_in*)p->ifa_addr;
            mask = *(struct sockaddr_in*)p->ifa_netmask;
            strncpy(km->iface[i].name, p->ifa_name, 32);
            km->iface[i].ip = ip.sin_addr.s_addr;
            km->iface[i].mask = mask.sin_addr.s_addr;
            for(j=0; j<MAX_IF_COUNT; ++j) if(ip.sin_addr.s_addr==k_if_ip(j))
            {
                km->iface[i].mac_hi = *(WORD*)k_if_hwaddr(j);
                km->iface[i].mac_lo = *(DWORD*)(k_if_hwaddr(j)+2);
                km->iface[i].gateway = k_if_gw(j);
                km->iface[i].dns = k_if_dns(j);
                break;
            }
        }
        if(ifap) freeifaddrs(ifap);

        // Pick a default DNS: prefer a value already discovered on any
        // interface (nmcli), fall back to /etc/resolv.conf, finally 8.8.8.8.
        // Apps that query devno=0 (loopback) or a device without DNS still
        // need an answer.
        DWORD def_dns = 0, i;
        for(i=0; i<km->if_count; ++i) if(km->iface[i].dns!=0) { def_dns = km->iface[i].dns; break; }
        if(def_dns==0) def_dns = k_read_resolv_conf_dns();
        if(def_dns==0) def_dns = 8 | (8<<8) | (8<<16) | (8<<24); // 8.8.8.8
        for(i=0; i<km->if_count; ++i) if(km->iface[i].dns==0) km->iface[i].dns = def_dns;

        for(i=0; i<km->if_count; ++i) NET_DBG(
            "kex net: iface[%u] name=%s ip=%u.%u.%u.%u dns=%u.%u.%u.%u\n",
            i, km->iface[i].name,
            km->iface[i].ip&0xFF, (km->iface[i].ip>>8)&0xFF, (km->iface[i].ip>>16)&0xFF, (km->iface[i].ip>>24)&0xFF,
            km->iface[i].dns&0xFF, (km->iface[i].dns>>8)&0xFF, (km->iface[i].dns>>16)&0xFF, (km->iface[i].dns>>24)&0xFF);
    }
}

DWORD k_net_info(k_context* ctx, BYTE devNo, BYTE func, DWORD* ebx, DWORD* ecx)
{
    k_net_update();
    KERNEL_MEM* km = kernel_mem(); if(func!=255 && devNo>=km->if_count) return -1;
    switch(func)
    {
    case 0: return devNo==0 ? 0 : 1; // type
    case 1: strcpy(user_mem(*ecx), km->iface[devNo].name); return 0;
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
    case 4: {
        struct sockaddr_in* sa = user_mem(edx);
        NET_DBG("kex net: connect fd=%u len=%u family=%u port=%u ip=%u.%u.%u.%u\n",
            ecx, esi, sa->sin_family, ntohs(sa->sin_port),
            ((BYTE*)&sa->sin_addr)[0], ((BYTE*)&sa->sin_addr)[1],
            ((BYTE*)&sa->sin_addr)[2], ((BYTE*)&sa->sin_addr)[3]);
        ret = connect(ecx, user_mem(edx), esi);
        if(ret==-1) NET_DBG("kex net: connect errno=%d (%s)\n", errno, strerror(errno));
        break;
    }
    case 5: ret = accept(ecx, user_mem(edx), &esi); break;
    case 6: {
        BYTE* buf = user_mem(edx);
        ret = send(ecx, buf, esi, edi);
        NET_DBG("kex net: send fd=%u len=%u -> %d%s%s\n", ecx, esi, (int)ret,
            ret==-1?" errno=":"", ret==-1?strerror(errno):"");
        if(esi>=12 && esi<=512) {
            // Try to decode DNS query name from question section
            char host[256]; int hp=0, i=12, lbl;
            while(i<(int)esi && (lbl=buf[i])!=0 && hp<250) {
                if(lbl>63 || i+1+lbl>(int)esi) { hp=0; break; }
                if(hp) host[hp++]='.';
                memcpy(host+hp, buf+i+1, lbl); hp+=lbl; i+=1+lbl;
            }
            host[hp]=0;
            if(hp>0) NET_DBG("kex net: dns query='%s' rd=%u\n", host, buf[2]&1);
        }
        break;
    }
    case 7: {
        BYTE* buf = user_mem(edx);
        ret = recv(ecx, buf, esi, edi);
        NET_DBG("kex net: recv fd=%u buflen=%u -> %d%s%s\n", ecx, esi, (int)ret,
            ret==-1?" errno=":"", ret==-1?strerror(errno):"");
        if(ret>=12 && ret<=512 && esi<=512) {
            // DNS reply: decode RCODE and answer count
            int rcode = buf[3]&0xF;
            int ancount = (buf[6]<<8) | buf[7];
            const char* rc_name = rcode==0?"NOERROR":rcode==1?"FORMERR":rcode==2?"SERVFAIL":
                rcode==3?"NXDOMAIN":rcode==4?"NOTIMP":rcode==5?"REFUSED":"?";
            NET_DBG("kex net: dns reply rcode=%d(%s) ancount=%d\n", rcode, rc_name, ancount);
        }
        if(ret==-1 && (edi&MSG_DONTWAIT)!=0) err=6;
        break;
    }
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
    case 0: *ebx = km->iface[devNo].mac_hi; return km->iface[devNo].mac_lo;
    default: return -1;
    }
}

DWORD kp_ipv4(k_context* ctx, BYTE devNo, BYTE func, DWORD ecx)
{
    KERNEL_MEM* km = kernel_mem();
    if(func==4)
    {
        // "Get DNS" — return a usable value regardless of devNo. Kolibri apps
        // sometimes pass devNo=0 (loopback) or a devNo that doesn't have DNS
        // populated (VPN/docker). Search every interface for a valid DNS.
        DWORD i, dns = devNo<km->if_count ? km->iface[devNo].dns : 0;
        if(dns==0) for(i=0; i<km->if_count; ++i) if(km->iface[i].dns) { dns = km->iface[i].dns; break; }
        NET_DBG("kex net: get_dns devno=%u -> %u.%u.%u.%u\n",
            devNo, dns&0xFF, (dns>>8)&0xFF, (dns>>16)&0xFF, (dns>>24)&0xFF);
        return dns;
    }
    if(devNo>=km->if_count) return -1;
    switch(func)
    {
    case 2: return km->iface[devNo].ip;
    case 3: km->iface[devNo].ip = ecx; return 0;
    case 5: km->iface[devNo].dns = ecx; return 0;
    case 6: return km->iface[devNo].mask;
    case 7: km->iface[devNo].mask = ecx; return 0;
    case 8: return km->iface[devNo].gateway;
    case 9: km->iface[devNo].gateway = ecx; return 0;
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
