#include "k_mem.h"
#include "k_event.h"
#include "k_iconv.h"
#include "k_ipc.h"
#include "k_file.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include <utime.h>
#include <sys/stat.h>

#pragma pack(push,1)
typedef struct
{
    DWORD func;
    DWORD offset;
    DWORD extra;
    DWORD len;
    DWORD addr;
    BYTE name[4];
} k_fcb;

typedef struct
{
    BYTE sec;
    BYTE min;
    BYTE hour;
    BYTE reserved;
    BYTE day;
    BYTE mon;
    WORD year;
} k_time;

typedef struct
{
    DWORD attr;
    DWORD cp;
    k_time ctime;
    k_time atime;
    k_time mtime;
    QWORD size;
} k_folder_item_base;

typedef struct
{
    k_folder_item_base f;
    char fname[264];
} k_folder_item;

typedef struct
{
    k_folder_item_base f;
    char fname[520];
} k_folder_item2;

typedef struct
{
    DWORD version;
    DWORD count;
    DWORD total;
    DWORD reserved[5];
    k_folder_item items[1];
} k_folder;
#pragma pack(pop)

char k_root[1024];

int is_dev_name(BYTE* name)
{
    int i;
    for(i=0; *name; ++name) if(*name=='/' && name[1]!=0) ++i;
    return i<=2;
}

int is_key(BYTE** p)
{
    char* key = kernel_mem()->extfs_key; int len = strlen(key);
    if(len>0 && strncasecmp((char*)*p, key, len)==0 && (p[0][len]=='/'||p[0][len]==0))
    {
        *p += len; return 2;
    }
    else if(strncasecmp((char*)*p, "sys", 3)==0 && (p[0][3]=='/'||p[0][3]==0))
    {
        *p += 3;  return 1;
    }
    return 0;
}

char* k_parse_item(k_context* ctx, BYTE* name, int cp, char* buf, char* ebuf)
{
    if(*name=='/')
    {
        name += cp==2?2:1;
        switch(is_key(&name))
        {
        case 1: buf = k_parse_item(ctx, (BYTE*)"/RD/1/", cp, buf, ebuf); break;
        case 2: buf = k_parse_item(ctx, (BYTE*)kernel_mem()->extfs_path, cp, buf, ebuf); break;
        default: strcpy(buf, k_root); buf = strchr(buf,0); break;
        }
    }
    else
    {
        buf = k_parse_item(ctx, ctx==NULL?(BYTE*)"/RD/1/":ctx->curpath, cp, buf, ebuf);
    }
    if (buf[-1]!='/') *buf++='/';
    if(k_strsize(name,cp,3)<=ebuf-buf) k_strcpy((BYTE*)buf,3,name,cp); else *buf = 0;
    return strchr(buf,0);
}

void k_check_exists(char* fname)
{
    if(access(fname,F_OK)==0) return;
    char* p = strchr(fname,0);
    if(p==fname) return; else --p;
    while(p!=fname && *p!='/') --p;
    if(p==fname) return; else *p = 0;
    k_check_exists(fname);
    if(p[1]!=0)
    {
        struct dirent* e; DIR* d = opendir(fname);
        if(d!=NULL)
        {
            while((e=readdir(d))!=NULL)
            {
                if(strcasecmp(p+1, e->d_name)==0)
                {
                    strcpy(p+1, e->d_name);
                    break;
                }
            }
            closedir(d);
        }
    }
    *p = '/';
}

void k_parse_name(k_context* ctx, BYTE* name, int cp, char* buf, int buflen)
{
    if(cp==0 && *name>0 && *name<4) cp = *name++;
    k_parse_item(ctx, name, cp, buf, buf+buflen);
    k_check_exists(buf);
}

DWORD k_path_len(BYTE* path, int cp)
{
    BYTE* p=path; if(cp<0 && *p>0 && *p<4) cp = *p++;
    if(cp==2)
    {
        if(*p=='/' && p[1]==0) { p+=2; is_key(&p); }
        while(*(WORD*)p) p+=2; p+=2;
    }
    else
    {
        while(*p++);
    }
    return p-path;
}

void set_time(k_time* t, time_t* from)
{
    struct tm* ft = localtime(from);
    t->sec = ft->tm_sec;
    t->min = ft->tm_min;
    t->hour = ft->tm_hour;
    t->reserved = 0;
    t->day = ft->tm_mday;
    t->mon = ft->tm_mon+1;
    t->year = ft->tm_year;
}

int get_stat(char* fname, k_folder_item_base* f)
{
    struct stat st; 
    if(stat(fname, &st)<0) return -1;
    if(f!=NULL)
    {
        f->attr = S_ISDIR(st.st_mode)?0x10:0;
        f->size = st.st_size;
        set_time(&f->ctime, &st.st_ctime);
        set_time(&f->atime, &st.st_atime);
        set_time(&f->mtime, &st.st_mtime);
    }
    return 0;
}

DWORD k_read_file(char* fname, k_fcb* fcb, DWORD* count)
{
    void* data = user_mem(fcb->addr);
    FILE* f = fopen(fname, "r"); if(f==NULL) return 5;
    if(fcb->offset>0) fseek(f, fcb->offset, SEEK_SET);
    *count = fread(data, 1, fcb->len, f); fclose(f);
    return *count<fcb->len?6:0;
}

DWORD k_write_file(char* fname, k_fcb* fcb, DWORD* count, int create)
{
    void* data = user_mem(fcb->addr);
    FILE* f = fopen(fname, create?"wb":"rb+"); if(f==NULL) return 5;
    if(fcb->offset>0 && !create) fseek(f, fcb->offset, SEEK_SET);
    *count = fwrite(data, 1, fcb->len, f); fclose(f);
    return *count<fcb->len?8:0;
}

DWORD k_truncate_file(char* fname, k_fcb* fcb)
{
    FILE* f = fopen(fname, "rb+"); if(f==NULL) return 5;
    int err = ftruncate(fileno(f), fcb->offset); fclose(f);
    return err?8:0;
}

#define MAX_DEVICES 15
char k_dev_name[MAX_DEVICES+1][10];

char* k_readroot(DIR* d, int i)
{
    if(k_dev_name[0][0]==0)
    {
        static char* tmpl[] = {"RD","FD","HD","HD%d","TMP%d","CD%d"};
        int t,n,dn=0; char* buf = strchr(k_root,0);
        for(t=0; t<sizeof(tmpl)/sizeof(*tmpl) && dn<MAX_DEVICES; ++t)
        {
            int nmax = strchr(tmpl[t],'%') ? 4 : 1;
            for(n=0; n<nmax; ++n)
            {
                sprintf(buf, tmpl[t], n);
                if(access(k_root,F_OK)==0) strcpy(k_dev_name[dn++], buf);
            }
        }
        *buf = 0;
    }
    return *k_dev_name[i] ? k_dev_name[i] : NULL;
}

char* k_readdir(DIR* d, int i)
{
    struct dirent* e = readdir(d);
    return e ? e->d_name : NULL;
}

DWORD k_read_folder(char* fname, k_fcb* fcb, DWORD* count, int skipdot, int root)
{
    union { k_folder_item* item; k_folder_item2* item2; } p; int i;
    k_folder* data = (k_folder*)user_mem(fcb->addr); p.item = data->items; *count = 0;
    DIR* d = NULL; char* e,*(*_next)(DIR*,int);

    data->version = 1;
    data->count = 0;
    data->total = 0;

    _next = root ? k_readroot : k_readdir;
    if(root==0 && (d=opendir(fname))==NULL) return 5;
    for(i=0;; ++i)
    {
        e = _next(d,i); if(e==NULL) break;
        while(e!=NULL && (strcmp(e,".")==0 || (skipdot && strcmp(e,"..")==0))) e = _next(d,i);
        if(e==NULL) break;
        if(i>=fcb->offset && i<fcb->offset+fcb->len)
        {
            char buf[1024],*s; sprintf(buf, "%s/%s", fname, e);
            get_stat(buf, &p.item->f);
            p.item->f.cp = fcb->extra;
            strcpy(p.item->fname, e);
            if(root) for(s=p.item->fname; *s; ++s) *s = tolower(*s);
            ++*count; if(fcb->extra<2) ++p.item; else ++p.item2;
        }
        data->total++;
    }
    if(d) closedir(d);

    data->count = *count;

    return *count<fcb->len?6:0;
}

DWORD k_stat(char* fname, k_fcb* fcb)
{
    k_folder_item_base* f = (k_folder_item_base*)user_mem(fcb->addr);
    f->cp = fcb->extra;
    return get_stat(fname, f)<0 ? 5 : 0;
}

time_t convert_time(k_time* from)
{
    struct tm st;
    st.tm_sec = from->sec;
    st.tm_min = from->min;
    st.tm_hour = from->hour;
    st.tm_mday = from->day;
    st.tm_mon = from->mon-1;
    st.tm_year = from->year;
    return mktime(&st);
}

DWORD k_set_attr(char* fname, k_fcb* fcb)
{
    k_folder_item_base* f = (k_folder_item_base*)user_mem(fcb->addr);
    struct utimbuf tb;
    tb.actime = convert_time(&f->atime);
    tb.modtime = convert_time(&f->mtime);
    utime(fname, &tb);
    chmod(fname, f->attr&1?0444:0666);
    return 0;
}

DWORD k_run_app(k_context* ctx, char* fname, char* args)
{
    msg_t msg;
    if(get_stat(fname,NULL)<0) return -5;
    int len = strlen(k_root);
    if(strncmp(k_root, fname, len)==0) fname += len-1;
    msg_run(&msg, fname, args);
    write_msg(ipc_server, &msg);
    for(ctx->retcode = 0; ctx->retcode==0;) k_process_ipc_event(ctx, &msg);
    return ctx->retcode;
}

DWORD k_unlink(char* fname)
{
    struct stat st;  if(stat(fname, &st)<0) return 5;
    if(S_ISDIR(st.st_mode))
    {
        if(rmdir(fname)==0) return 0;
    }
    else
    {
        if(unlink(fname)==0) return 0;
    }
    return 5;
}

DWORD k_mkdir(char* fname)
{
    return mkdir(fname, 0777)==0 ? 0 : 5;
}

DWORD k_move(char* fname, char* newname)
{
    return rename(fname, newname)==0 ? 0 : 5;
}

DWORD k_file_syscall(k_context* ctx, DWORD* eax, DWORD* ebx, int f80)
{
    int cp = 0; BYTE* name; char fname[1024];
    k_fcb* fcb = (k_fcb*)user_mem(*ebx);
    if(f80)
    {
        DWORD* p = (DWORD*)fcb->name;
        cp = *p++; name = user_pb(*p);
    }
    else
    {
        name = fcb->name[0]!=0 ? fcb->name : user_pb(*(DWORD*)(fcb->name+1));
    }
    if(cp==0 && *name>0 && *name<4) cp = *name++;
    k_parse_name(ctx, name, cp, fname, sizeof(fname));
    switch(fcb->func)
    {
    case 0: *eax = k_read_file(fname, fcb, ebx); break;
    case 1: *eax = k_read_folder(fname, fcb, ebx, is_dev_name(name), memcmp(name,"/\x0\x0",cp==2?4:2)==0); break;
    case 2: case 3: *eax = k_write_file(fname, fcb, ebx, fcb->func==2); break;
    case 4: *eax = k_truncate_file(fname, fcb); break;
    case 5: *eax = k_stat(fname, fcb); break;
    case 6: *eax = k_set_attr(fname, fcb); break;
    case 7: *eax = k_run_app(ctx, fname, fcb->extra==0?NULL:user_mem(fcb->extra)); break;
    case 8: *eax = k_unlink(fname); break;
    case 9: *eax = k_mkdir(fname); break;
    case 10: *eax = k_move(fname, (char*)user_mem(fcb->extra)); break;
    default: *eax = 2; return 1;
    }
    return 0;
}

void k_copy_path(BYTE* to, BYTE* from)
{
    memcpy(to, from, k_path_len(from, -1));
}

DWORD k_set_curpath(k_context* ctx, BYTE* path, int cp)
{
    DWORD len = k_path_len(path, cp);
    BYTE* p = ctx->curpath; if(cp>=0) *p++ = cp;
    if(len>254) len = 254; memcpy(p, path, len);
    return 0;
}

DWORD k_get_curpath(k_context* ctx, BYTE* path, int cp, DWORD len)
{
    DWORD curlen = k_path_len(ctx->curpath, -1); int curcp = -1;
    BYTE* p = ctx->curpath; if(*p>1 && *p<4) curcp = *p++;
    if(curcp!=cp) k_panic("Path conversion not supported");
    if(curlen>len) curlen = len;
    memcpy(path, p, curlen);
    return 0;
}

DWORD k_set_extfs(BYTE* data)
{
    KERNEL_MEM* km = kernel_mem();
    memcpy(km->extfs_key, data, 128);
    memmove(km->extfs_path+1, km->extfs_path, 63);
    km->extfs_path[0] = '/';
    return 0;
}

DWORD k_load_skin(k_context* ctx, BYTE* name)
{
    char fname[1024]; msg_t msg;
    k_parse_name(ctx, name, 0, fname, sizeof(fname));
    msg_load_skin(&msg, fname);
    write_msg(ipc_server, &msg);
    //for(ctx->retcode = 0; ctx->retcode==0;) k_process_ipc_event(ctx, &msg);
    return 0; // ctx->retcode
}
