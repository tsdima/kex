#include "k_mem.h"
#include "k_event.h"
#include "k_file.h"
#include "k_proc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lzma.h>

#pragma pack(push,1)
typedef struct
{
    BYTE props; // (pb*5+lp)*9+lc
    DWORD dict_size;
    QWORD uncomp_size;
    BYTE data;
} k_lzma_hdr;
#pragma pack(pop)

static char* lzma_error = "lzma error";

void k_unpack_lzma(DWORD in_size, BYTE* in, DWORD out_size, BYTE* out)
{
    k_lzma_hdr hdr = {(2*5+0)*9+3,0x10000,-1,0};
    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_alone_decoder(&strm, UINT64_MAX);
    if(ret!=LZMA_OK) k_panic(lzma_error);
    while(hdr.dict_size<out_size) hdr.dict_size<<=1;
    strm.avail_out = out_size;
    strm.next_out = out;
    strm.avail_in = sizeof(hdr);
    strm.next_in = &hdr.props;
    ret = lzma_code(&strm, LZMA_RUN);
    if(ret!=LZMA_OK) k_panic(lzma_error);
    BYTE b=in[0]; in[0]=in[3]; in[3]=b; b=in[1]; in[1]=in[2]; in[2]=b;
    strm.avail_in = in_size;
    strm.next_in = in;
    ret = lzma_code(&strm, LZMA_FINISH);
    if(ret!=LZMA_OK) k_panic(lzma_error);
}

void k_tricks(DWORD size, BYTE* data, BYTE cmp, int version)
{
    DWORD i,tmp;
    for(i=0; i<size; ++i)
    {
        if(version==2 && data[i]==0x0F && data[i+1]>=0x80 && data[i+1]<0x90 && data[i+2]==cmp) i+=2; else
        if((data[i]&0xFE)==0xE8 && data[i+1]==cmp) ++i; else continue;
        tmp = (((data[i+1]<<8)|data[i+2])<<8)|data[i+3];
        *(DWORD*)(data+i) = tmp-(i+4); i+=3;
    }
}

DWORD coff_parse(COFF_HEADER* hdr, BYTE* mem, DWORD* exports)
{
    DWORD i,j,size=0;
    COFF_SECTION* sec = (COFF_SECTION*)(sizeof(COFF_HEADER)+(BYTE*)hdr);
    COFF_SYM* sym = (COFF_SYM*)(hdr->pSymTable+(BYTE*)hdr);
    for(i=0; i<hdr->nSection; ++i,++sec)
    {
        DWORD align = (sec->Flags>>20)&15;
        if(align<1||align>12) align = 12;
        align = (1<<align)-1;
        size = (size+align)&~align;
        if(mem)
        {
            sec->VirtualAddress = (mem-user_pb(0))+size;
            if(exports)
            {
                if(sec->PtrRawData==0)
                {
                    memset(mem+size, 0, sec->SizeOfRawData);
                }
                else
                {
                    memcpy(mem+size, sec->PtrRawData+(BYTE*)hdr, sec->SizeOfRawData);
                }
                COFF_RELOC* rel = (COFF_RELOC*)(sec->PtrReloc+(BYTE*)hdr);
                for(j=0; j<sec->NumReloc; ++j,++rel)
                {
                    DWORD value = sym[rel->SymIndex].Value;
                    DWORD* pfix = (DWORD*)(mem+size+rel->VirtualAddress);
                    switch(rel->Type)
                    {
                    case 6: *pfix += value; break;
                    case 20: *pfix += value-(((BYTE*)pfix)-user_pb(0))-4; break;
                    }
                }
            }
        }
        size += sec->SizeOfRawData;
    }
    if(exports)
    {
        for(i=0; i<hdr->nSymbols; ++i,++sym)
        {
            if(strcmp(sym->name, "EXPORTS")==0)
            {
                *exports = sym->Value;
                break;
            }
        }
    }
    else if(mem)
    {
        COFF_SECTION* sec = (COFF_SECTION*)(sizeof(COFF_HEADER)+(BYTE*)hdr);
        for(i=0; i<hdr->nSymbols; ++i,++sym)
        {
            if(sym->SectionNumber>0 && sym->SectionNumber<0xFFFE)
            {
                sym->Value += sec[sym->SectionNumber-1].VirtualAddress;
            }
        }
    }
    return size;
}

void* k_malloc_wrap(DWORD size)
{
    return malloc(size);
}

void* k_load(k_context* ctx, BYTE* name, int cp, void*(*_mem_alloc)(DWORD), DWORD* exports, DWORD* psize)
{
    void* mem = NULL; if(_mem_alloc == NULL) _mem_alloc = k_malloc_wrap;

    FILE* fp = fopen((char*)name, "rb");
    if(fp == NULL)
    {
        char fname[512]; k_parse_name(ctx, name, cp, fname, sizeof(fname));
        fp = fopen(fname, "rb");
    }
    if(fp != NULL)
    {
        KEX_FILE_HDR hdr; DWORD size,flen; BYTE* unpacked = NULL;
        if(fread(&hdr, 1, sizeof(hdr), fp)!=sizeof(hdr)) { fclose(fp); return 0; }
        fseek(fp, 0, SEEK_END); size = flen = ftell(fp);
        if(size==-1) { fclose(fp); return 0; }
        if(memcmp(hdr.magic,"KPCK",4) == 0)
        {
            size = hdr.u.kpck.unpacked; unpacked = (BYTE*)malloc(size);
            BYTE* packed = (BYTE*)malloc(flen-12);
            fseek(fp, 12, SEEK_SET);
            fread(packed, 1, flen-12, fp);
            switch(hdr.u.kpck.flags&0x3F) {
            case 0: memcpy(unpacked, packed, size); break;
            case 1: k_unpack_lzma(flen-12, packed, size, unpacked); break;
            default: k_panic("unsupported pack method"); break;
            }
            switch(hdr.u.kpck.flags&0xC0) {
            case 0x40: k_tricks(size, unpacked, packed[flen-13], 1); break;
            case 0x80: k_tricks(size, unpacked, packed[flen-13], 2); break;
            case 0xC0: k_panic("unsupported trick"); break;
            }
            free(packed);
            memcpy(&hdr, unpacked, sizeof(hdr));
        }
        if(psize) *psize = size;
        if(0x14C == *(WORD*)&hdr)
        {
            size = coff_parse((COFF_HEADER*)unpacked, NULL, NULL);
            if(size==0||size>=0x1000000) k_panic("coff error");
            mem = _mem_alloc(size);
            coff_parse((COFF_HEADER*)unpacked, (BYTE*)mem, NULL);
            coff_parse((COFF_HEADER*)unpacked, (BYTE*)mem, exports);
            //fclose(fp); fp=fopen("../0_unpacked","wb"); fwrite(mem,size,1,fp);
        }
        else
        {
            if(memcmp(hdr.magic,"MENUET0",7) == 0)
            {
                mem = _mem_alloc(hdr.u.menuet.ram_size);
            }
            else
            {
                mem = _mem_alloc(size);
            }
            if(unpacked)
            {
                memcpy(mem, unpacked, size);
            }
            else
            {
                fseek(fp, 0, SEEK_SET);
                fread(mem, 1, size, fp);
            }
        }
        if(unpacked) free(unpacked);
        fclose(fp);
    }
    else
    {
        fprintf(stderr, "%s not found\n", name);
    }

    return mem;
}

DWORD k_load_file(k_context* ctx, BYTE* name, int cp, DWORD* psize)
{
    DWORD exports = 0; BYTE* mem; if(psize) *psize = 0;
    mem = k_load(ctx, name, cp, k_mem_alloc_from_heap, &exports, psize);
    return mem ? mem - user_pb(0) : 0;
}

DWORD k_load_dll(k_context* ctx, BYTE* name, int cp)
{
    DWORD exports = 0;
    k_load(ctx, name, cp, k_mem_alloc_from_heap, &exports, NULL);
    return exports;
}

void k_exec(k_context* ctx, char* kexfile, char* args)
{
    KEX_FILE_HDR* hdr = (KEX_FILE_HDR*)k_load(ctx, (BYTE*)kexfile, 0, k_mem_alloc, NULL, NULL);
    ctx->memsize = k_mem_get_size();

    if(hdr != NULL && memcmp(hdr->magic,"MENUET0",7) == 0)
    {
        DWORD esp = hdr->u.menuet.ram_size;
        if (hdr->magic[7]!='0')
        {
            esp = hdr->u.menuet.stack_pos;
            if(hdr->u.menuet.path_buf!=0)
            {
                char* path_buf = (char*)user_mem(hdr->u.menuet.path_buf);
                if(*kexfile!='/') { strcpy(path_buf, "/sys/"); path_buf+=5; }
                strcpy(path_buf, kexfile);
            }

            if(args!=NULL && hdr->u.menuet.args_buf!=0)
            {
                char* args_buf = (char*)user_mem(hdr->u.menuet.args_buf);
                strncpy(args_buf, args, 256);
            }
        }

        k_start_thread(hdr->u.menuet.start, esp);
   }
}

void k_start_thread(DWORD eip, DWORD esp)
{
#ifdef __x86_64__
     __asm__ __volatile__ (
        "mov $0x17, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "push %0\n"
        "lret\n" : :
        "r" (k_stub_jmp(eip, esp)+0xf00000000) : "ax"
    );
#else
    k_stub_jmp(eip, esp);
     __asm__ __volatile__ (
        "mov $0x17, %ax\n"
        "mov %ax, %ds\n"
        "mov %ax, %es\n"
        "mov %ax, %ss\n"
        "jmp $0x0f, $0x3FFFF000\n"
    );
#endif
}
