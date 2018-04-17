#include "k_mem.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <asm/ldt.h>
#include <fcntl.h>

int modify_ldt(int func, void* ptr, unsigned long bytecount);

#define HEAP_BLOCK_SIZE 0x100000

#define HTAG_NONE   0x00
#define HTAG_SZMASK 0x0F
#define HTAG_BIGSZ  0x0F
#define HTAG_NORMAL 0x10
#define HTAG_FREE   0x20
#define HTAG_PARAM  0x80

#define KERNEL_SHMEM "/kolibri.kmem"
#define KERNEL_MEM_BASE (void*)0x10000000
#define KERNEL_MEM_SIZE ((sizeof(KERNEL_MEM)+0xFFF)&-0x1000)

#define SKIN_SHMEM "/kolibri.skin"
#define SKIN_BASE (void*)0x11000000

#define TLS_BASE (void*)0x11FFF000L
#define TLS_SIZE 0x1000

#define CLIPBOARD_SHMEM "/kolibri.clip.%d"
#define CLIPBOARD_BASE (void*)0x12000000

#define HCTRL_SHMEM "/kolibri.heap.%d"
#define HCTRL_MEM_BASE (void*)0x1FFC0000
#define HCTRL_MEM_MAXSIZE 0x40000

#define APP_SHMEM "/kolibri.app.%d"
#define KEX_BASE (void*)0x20000000L
#define KEX_SIZE 0x40000000L

#define STUB_MEM_BASE (void*)0x5FFFF000
#define STUB_MEM_SIZE 0x1000

#define USER_SHMEM "/kolibri.usm.%d"
#define USER_SHMEM_START 0x3F000000

KERNEL_MEM* k_kernel_mem = NULL;

BYTE* k_base = NULL; DWORD k_base_size = 0, k_shmid = 0;
BYTE* k_heap_map = NULL; DWORD k_heap_map_size;
BYTE* k_skin_data = NULL; DWORD k_skin_size;
BYTE* k_stub = NULL; BYTE* k_tls_base = NULL;

char _init_keyboard_layout[3][128]={
    "6\x1B"
    "1234567890-=\x8\x9"
    "qwertyuiop[]\xD"
    "~asdfghjkl;\x27\x60\x0\\zxcvbnm,./\x0\x34\x35 "
    "@234567890123\xB4\xB2\xB8\x36\xB0\x37"
    "\xB3\x38\xB5\xB1\xB7\xB9\xB6"
    "AB<D\377FGHIJKLMNOPQRSTUVWXYZ"
    "ABCDEFGHIJKLMNOPQR",

    "6\x1B"
    "!@#$%^&*()_+\x8\x9"
    "QWERTYUIOP{}\xD"
    "~ASDFGHJKL:\"~\x0|ZXCVBNM<>?\x0\x34\x35 "
    "@234567890123\xB4\xB2\xB8\x36\xB0\x37"
    "\xB3\x38\xB5\xB1\xB7\xB9\xB6"
    "AB>D\377FGHIJKLMNOPQRSTUVWXYZ"
    "ABCDEFGHIJKLMNOPQR",

    " \x1B"
    " @ $  {[]}\\ \x8\x9"
    "            \xD"
    "             \x0           \x0\x34\x0 "
    "             \xB4\xB2\xB8\x36\xB0\x37"
    "\xB3\x38\xB5\xB1\xB7\xB9\xB6"
    "ABCD\377FGHIJKLMNOPQRSTUVWXYZ"
    "ABCDEFGHIJKLMNOPQR"
};

void* k_shmem_open(char* name, int shmid, void* base, DWORD size, int prot, DWORD* psize)
{
    char buf[64]; sprintf(buf, name, shmid); int fd;
    if (psize != NULL)
    {
        fd = shm_open(buf, O_RDWR, 0660); if(fd < 0) return NULL;
        struct stat st; fstat(fd, &st); *psize = size = st.st_size;
    }
    else
    {
        fd = shm_open(buf, O_CREAT|O_RDWR, 0660);
        ftruncate(fd, size);
    }
    void* ret = mmap(base, size, PROT_READ|PROT_WRITE|prot, MAP_SHARED|MAP_FIXED, fd, 0);
    close(fd); return ret;
}

void* k_shmem_resize(char* name, int shmid, void* base, DWORD oldsize, DWORD size)
{
    char buf[64]; sprintf(buf, name, shmid);
    int fd = shm_open(buf, O_RDWR, 0660); ftruncate(fd, size);
    void* ret = mremap(base, oldsize, size, 0);
    close(fd); return ret;
}

void k_shmem_unlink(char* name, int shmid)
{
    char buf[64]; sprintf(buf, name, shmid); shm_unlink(buf);
}

void k_modify_ldt()
{
    struct user_desc ldt_code = {1, (unsigned long)k_base, KEX_SIZE>>12, 1, MODIFY_LDT_CONTENTS_CODE, 0, 1, 0, 1};
    struct user_desc ldt_data = {2, (unsigned long)k_base, KEX_SIZE>>12, 1, MODIFY_LDT_CONTENTS_DATA, 0, 1, 0, 1};
    struct user_desc ldt_tls  = {3, (unsigned long)k_tls_base, TLS_SIZE>>12, 1, MODIFY_LDT_CONTENTS_DATA, 0, 1, 0, 1};

    modify_ldt(1, &ldt_code, sizeof(ldt_code));
    modify_ldt(1, &ldt_data, sizeof(ldt_data));
    modify_ldt(1, &ldt_tls, sizeof(ldt_tls));
}

void k_kernel_mem_open()
{
    k_kernel_mem = (KERNEL_MEM*)k_shmem_open(KERNEL_SHMEM, k_shmid, KERNEL_MEM_BASE, KERNEL_MEM_SIZE, 0, NULL);
}

void k_kernel_mem_init()
{
    k_kernel_mem_open();
    memset(k_kernel_mem, 0, sizeof(KERNEL_MEM));
    memcpy(k_kernel_mem->keyboard_layout, _init_keyboard_layout, sizeof(_init_keyboard_layout));
    k_kernel_mem->keyboard_country = 1;
    k_kernel_mem->keyboard_language = 1;
    k_kernel_mem->mouse_dbl_click_delay = 50;
    k_kernel_mem->pci_enabled = 1;
}

void k_kernel_mem_cleanup()
{
    k_shmem_unlink(KERNEL_SHMEM, k_shmid);
    k_shmem_unlink(SKIN_SHMEM, k_shmid);
}

KERNEL_MEM* kernel_mem()
{
    return k_kernel_mem;
}

void* user_mem(DWORD addr)
{
    return k_base+addr;
}

void k_mem_init(int shmid)
{
    k_shmid = shmid;
    k_kernel_mem_open();
    k_stub = (BYTE*)mmap(STUB_MEM_BASE, STUB_MEM_SIZE, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, 0, 0);
    k_tls_base = (BYTE*)mmap(TLS_BASE, TLS_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, 0, 0);
}

void k_mem_done(int shmid)
{
    k_shmem_unlink(APP_SHMEM, shmid);
    k_shmem_unlink(HCTRL_SHMEM, shmid);
}

void k_mem_reopen()
{
    k_base = k_shmem_open(APP_SHMEM, k_shmid, KEX_BASE, 0, PROT_EXEC, &k_base_size);
    k_heap_map = (BYTE*)k_shmem_open(HCTRL_SHMEM, k_shmid, HCTRL_MEM_BASE, 0, 0, &k_heap_map_size);
    k_modify_ldt();
}

void* k_mem_alloc(DWORD size)
{
    void* ptr; DWORD pages = ((size-1)>>12)+1; size = pages << 12;

    if (size <= k_base_size) return k_base;

    if (k_base == NULL)
    {
        ptr = k_shmem_open(APP_SHMEM, k_shmid, KEX_BASE, size, PROT_EXEC, NULL);
        if(ptr != MAP_FAILED) memset(ptr, 0, size);
    }
    else
    {
        ptr = k_shmem_resize(APP_SHMEM, k_shmid, k_base, k_base_size, size);
        if(ptr != MAP_FAILED) memset(k_base_size+(BYTE*)ptr, 0, size-k_base_size);
    }

    if (ptr != MAP_FAILED)
    {
        k_base = (BYTE*)ptr; k_base_size = size; k_modify_ldt();
    }

    return ptr;
}

void* k_mem_alloc_from_heap(DWORD size)
{
    DWORD addr = k_heap_alloc(size);
    return addr==0 ? NULL : k_base+addr;
}

void k_heap_set_param(BYTE* map, DWORD param)
{
    map[0] = HTAG_PARAM|(param&0x7F);
    map[1] = HTAG_PARAM|((param>>7)&0x7F);
    map[2] = HTAG_PARAM|((param>>14)&0x7F);
}

void k_heap_clr_param(BYTE* map)
{
    map[0] = HTAG_NONE;
    map[1] = HTAG_NONE;
    map[2] = HTAG_NONE;
}

DWORD k_heap_get_param(BYTE* map)
{
    return ((map[2]&0x7F)<<14)|((map[1]&0x7F)<<7)|(map[0]&0x7F);
}

DWORD k_heap_get_pcount(BYTE* map)
{
    DWORD pages = *map&HTAG_SZMASK;
    return pages < HTAG_BIGSZ ? pages : k_heap_get_param(map+1);
}

void k_heap_mark(BYTE tag, BYTE* map, DWORD pages)
{
    if (tag == HTAG_NONE)
    {
        if (*map != HTAG_NONE)
        {
            pages = *map&HTAG_SZMASK; *map = HTAG_NONE;
            if (pages == HTAG_BIGSZ)
            {
                pages = k_heap_get_param(map+1);
                k_heap_clr_param(map+1);
                k_heap_clr_param(map+pages-3);
            }
            else
            {
                map[pages-1] = HTAG_NONE;
            }
        }
    }
    else
    {
        if (pages < HTAG_BIGSZ)
        {
            map[0] = tag|pages;
            if (tag==HTAG_FREE) map[pages-1] = tag|pages;
        }
        else
        {
            map[0] = tag|HTAG_BIGSZ;
            k_heap_set_param(map+1, pages);
            if (tag==HTAG_FREE) k_heap_set_param(map+pages-3, pages);
        }
    }
}

BYTE* k_heap_find_free(DWORD pages)
{
    BYTE* map; DWORD pmax = k_base_size>>12;
    for(map = k_heap_map; map < k_heap_map+pmax; map += k_heap_get_pcount(map))
    {
        if (*map == HTAG_NONE)
        {
            return map+pages <= k_heap_map+pmax ? map : NULL;
        }
        if ((*map&HTAG_FREE)!=0 && k_heap_get_pcount(map)>=pages)
        {
            return map;
        }
    }
    return NULL;
}

void k_heap_setsize(DWORD size)
{
    DWORD pcount = ((size>>12)+0xFFF)&-0x1000;
    if (k_heap_map == NULL)
    {
        k_heap_map = (BYTE*)k_shmem_open(HCTRL_SHMEM, k_shmid, HCTRL_MEM_BASE, pcount, 0, NULL);
        memset(k_heap_map, 0, pcount);
        k_heap_mark(HTAG_NORMAL, k_heap_map, k_base_size>>12);
        k_heap_map_size = pcount;
    }
    else if(pcount>k_heap_map_size)
    {
        k_heap_map = (BYTE*)k_shmem_resize(HCTRL_SHMEM, k_shmid, k_heap_map, k_heap_map_size, pcount);
        memset(k_heap_map+k_heap_map_size, 0, pcount-k_heap_map_size);
        k_heap_map_size = pcount;
    }
    k_mem_alloc(size);
}

DWORD k_mem_size(DWORD size)
{
    if (k_heap_map!=NULL) return 1;
    return k_mem_alloc(size)==MAP_FAILED ? 1 : 0;
}

DWORD k_mem_get_size()
{
    DWORD i; if(k_heap_map==NULL) return k_base_size;
    for(i=(k_base_size>>12)-1; i>0 && k_heap_map[i]==0; --i);
    if(k_heap_map[i]&HTAG_NORMAL) i += k_heap_map[i]&HTAG_SZMASK; else ++i;
    return i<<12;
}

DWORD k_heap_init()
{
    if (k_heap_map == NULL) k_heap_setsize(((k_base_size-1)|(HEAP_BLOCK_SIZE-1))+1);
    return k_base_size;
}

DWORD k_heap_alloc_noclear(DWORD size)
{
    if (size==0) return 0;
    if (k_heap_map == NULL) k_heap_init();
    DWORD pages = ((size-1)>>12)+1;
    BYTE* map = k_heap_find_free(pages);
    if (map == NULL)
    {
        DWORD hbs = ((size-1)|(HEAP_BLOCK_SIZE-1))+1;
        k_heap_setsize(k_base_size+hbs);
        map = k_heap_find_free(pages);
        if(map == NULL) return 0;
    }
    if (*map == HTAG_NONE)
    {
        k_heap_mark(HTAG_NORMAL, map, pages);
    }
    else
    {
        DWORD pfree = k_heap_get_pcount(map)-pages;
        k_heap_mark(HTAG_NONE, map, 0);
        k_heap_mark(HTAG_NORMAL, map, pages);
        if (pfree>0) k_heap_mark(HTAG_FREE, map+pages, pfree);
    }
    return (map-k_heap_map)<<12;
}

DWORD k_heap_alloc(DWORD size)
{
    DWORD addr = k_heap_alloc_noclear(size);
    if(addr) memset(user_mem(addr), 0, size);
    return addr;
}

DWORD k_heap_free(DWORD addr)
{
    if (addr==0 || addr >= k_base_size) return 0;
    BYTE* map = k_heap_map + (addr>>12);
    if ((*map&HTAG_NORMAL)==0) return 0;
    DWORD pages = k_heap_get_pcount(map);
    k_heap_mark(HTAG_NONE, map, 0);
    if ((map[pages]&HTAG_FREE)!=0)
    {
        DWORD p2 = k_heap_get_pcount(map+pages);
        k_heap_mark(HTAG_NONE, map+pages, 0);
        pages += p2;
    }
    if ((map[-1]&HTAG_FREE)!=0)
    {
        DWORD p1 = map[-1]&HTAG_SZMASK; map -= p1;
        k_heap_mark(HTAG_NONE, map, 0);
        pages += p1;
    }
    else if ((map[-1]&HTAG_PARAM)!=0)
    {
        DWORD p1 = k_heap_get_param(map-3); map -= p1;
        k_heap_mark(HTAG_NONE, map, 0);
        pages += p1;
    }
    k_heap_mark(HTAG_FREE, map, pages);
    return 1;
}

DWORD k_heap_realloc(DWORD addr, DWORD size)
{
    if (addr==0) return k_heap_alloc(size);
    if (size==0 || addr >= k_base_size) return 0;
    BYTE* map = k_heap_map + (addr>>12);
    if ((*map&HTAG_NORMAL)==0) return 0;
    DWORD oldpages = k_heap_get_pcount(map);
    DWORD oldsize = oldpages<<12;
    if (size>0 && size<oldsize)
    {
        DWORD pages = ((size-1)>>12)+1;
        if(pages==oldpages) return addr;
        k_heap_mark(HTAG_NONE, map, 0);
        k_heap_mark(HTAG_NORMAL, map, pages);
        k_heap_mark(HTAG_FREE, map+pages, oldpages-pages);
        return addr;
    }
    else
    {
        k_heap_free(addr); if(size==0) return 0;
        DWORD newaddr = k_heap_alloc_noclear(size);
        if (newaddr==0) return 0;
        if (newaddr==addr) return addr;
        memmove(k_base+newaddr, k_base+addr, size<oldsize ? size : oldsize);
        return newaddr;
    }
}

#define USM_READ  0
#define USM_WRITE 1
#define USM_OPEN_ALWAYS 4
#define USM_CREATE 8

int k_find_usm(DWORD aname)
{
    if(aname>=k_base_size) return -1;
    int i; char* pname = user_mem(aname);
    k_user_shm* usm = k_kernel_mem->usm;
    for(i=0; i<MAX_USM; ++i) if(usm[i].tcount!=0 && strcmp(pname, usm[i].name)==0) return i;
    return -1;
}

int k_find_free_usm()
{
    k_user_shm* usm = k_kernel_mem->usm;
    int i; for(i=0; i<MAX_USM; ++i) if(usm[i].tcount==0) return i;
    return -1;
}

DWORD k_usm_open(k_context* ctx, DWORD aname, DWORD size, DWORD flags, DWORD* addr)
{
    int id = k_find_usm(aname),i; DWORD top = USER_SHMEM_START; *addr = 0;
    if(id<0 && (flags&(USM_CREATE|USM_OPEN_ALWAYS))==0) return 5;
    if(id>=0 && (flags&USM_CREATE)!=0) return 10;
    k_user_shm* usm = k_kernel_mem->usm; int clr = (id<0);
    if(id<0) {
        id = k_find_free_usm(); if(id<0) return 30;
        strncpy(usm[id].name, user_mem(aname), 31); usm[id].flags = flags;
    }
    else
    {
        if((USM_WRITE&flags&~usm[id].flags)!=0) return 10;
    }
    for(i=0; i<MAX_USM; ++i) if(ctx->usm_addr[i]!=0 && ctx->usm_addr[i]+ctx->usm_size[i]>top) top = ctx->usm_addr[i]+ctx->usm_size[i];
    BYTE* ptr = top+k_base; size = ((size-1)|0xFFF)+1;
    ptr = k_shmem_open(USER_SHMEM, id, ptr, size, 0, (flags&(USM_CREATE|USM_OPEN_ALWAYS))!=0 ? NULL : &size);
    ctx->usm_size[id] = size;
    ctx->usm_addr[id] = *addr = ptr-k_base;
    usm[id].tcount++;
    if(clr) memset(ptr, 0, size);
    return 0;
}

DWORD k_usm_close(k_context* ctx, DWORD aname)
{
    int id = k_find_usm(aname); if (id<0) return 5;
    k_user_shm* usm = k_kernel_mem->usm+id;
    munmap(user_mem(ctx->usm_addr[id]), ctx->usm_size[id]);
    ctx->usm_addr[id] = ctx->usm_size[id] = 0;
    if(usm->tcount--==0) k_shmem_unlink(USER_SHMEM, id);
    return 0;
}

void k_usm_clean(k_context* ctx)
{
    int i; k_user_shm* usm = k_kernel_mem->usm;
    for(i=0; i<MAX_USM; ++i) if(ctx->usm_addr[i]!=0)
        if(usm[i].tcount--==0) k_shmem_unlink(USER_SHMEM, i);
}

void* k_skin_alloc(DWORD size)
{
    if(k_skin_data!=NULL) k_skin_close();
    size = (size+0xFFF)&-0x1000;
    return k_skin_data = (BYTE*)k_shmem_open(SKIN_SHMEM, 0, SKIN_BASE, k_skin_size = size, 0, NULL);
}

BYTE* k_skin_open()
{
    if(k_skin_data!=NULL) k_skin_close();
    return k_skin_data = (BYTE*)k_shmem_open(SKIN_SHMEM, 0, SKIN_BASE, 0, 0, &k_skin_size);
}

void k_skin_close()
{
    if(k_skin_data==NULL) return;
    munmap(k_skin_data, k_skin_size); k_skin_data = NULL;
}

DWORD k_clipboard_add(DWORD size, DWORD addr)
{
    DWORD* ptr = k_shmem_open(CLIPBOARD_SHMEM, k_kernel_mem->clipboard_count++, CLIPBOARD_BASE, size, 0, NULL);
    if(ptr == MAP_FAILED) return 1;
    memcpy(ptr, user_pd(addr), size); *ptr = size;
    munmap(ptr, size);
    return 0;
}

DWORD k_clipboard_get(DWORD id)
{
    DWORD size,addr,*ptr = k_shmem_open(CLIPBOARD_SHMEM, id, CLIPBOARD_BASE, 0, 0, &size);
    if(ptr == MAP_FAILED) return 1;
    addr = k_heap_alloc((size+0xFFF)&-0x1000);
    memcpy(user_pd(addr), ptr, size);
    munmap(ptr, size);
    return addr;
}

DWORD k_clipboard_remove_last()
{
    if(k_kernel_mem->clipboard_count==0) return 1;
    k_shmem_unlink(CLIPBOARD_SHMEM, --k_kernel_mem->clipboard_count);
    return 0;
}

DWORD k_stub_resume(DWORD eip)
{
    // dd 17h / mov ss,[3FFFF000h] / jmp eip
    memcpy(k_stub, "\x17\x0\x0\x0\x8E\x15\x0\xF0\xFF\x3F\xE9", 11);
    *(DWORD*)(k_stub+11) = eip - 0x3FFFF00F;
    return 0x3FFFF004;
}

DWORD k_stub_jmp(DWORD eip, DWORD esp)
{
    // mov esp,# / jmp eip
    memcpy(k_stub, "\xBC\x0\x0\x0\x0\xE9", 6);
    *(DWORD*)(k_stub+1) = esp;
    *(DWORD*)(k_stub+6) = eip - 0x3FFFF00A;
    return 0x3FFFF000;
}

#ifdef __x86_64__

QWORD __k_fsbase;

#include <asm/prctl.h>

int arch_prctl(int code, ...);

void k_save_fsbase()
{
    arch_prctl(ARCH_GET_FS, &__k_fsbase);
}

void k_load_fsbase()
{
    arch_prctl(ARCH_SET_FS, __k_fsbase);
}

void k_set_fsbase()
{
    arch_prctl(ARCH_SET_FS, (QWORD)TLS_BASE);
}

#endif
