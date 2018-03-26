#include "k_mem.h"
#include "k_file.h"
#include "k_iconv.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

BYTE* font9bmp = NULL; DWORD font9len = 0;
BYTE* font16bmp = NULL; DWORD font16len = 0;

void* load_file(char* name, DWORD* plen)
{
    FILE* fp = fopen(name, "rb"); if(fp==NULL) return NULL;
    fseek(fp, 0, SEEK_END); DWORD len = ftell(fp); fseek(fp, 0, SEEK_SET);
    void* data = malloc(len); fread(data, 1, len, fp); fclose(fp);
    if(plen) *plen = len;
    return data;
}

void k_iconv_init()
{
    char name[1024],*p; strcpy(name, k_root); p = strchr(name,0);
    strcpy(p, "../char.mt"); font9bmp = load_file(name, &font9len);
    strcpy(p, "../charUni.mt"); font16bmp = load_file(name, &font16len);
}

DWORD ic_get_cp866(BYTE** p)
{
    DWORD ch = *(*p)++; if(ch<0x80) return ch;
    if(ch<0xB0) return ch+0x410-0x80;
    if(ch<0xE0) return '?';
    if(ch<0xF0) return ch+0x440-0xE0;
    if(ch==0xF0) return 0x401;
    if(ch==0xF1) return 0x451;
    return '?';
}

DWORD ic_put_cp866(BYTE* p, DWORD ch)
{
    if(ch<0x80) *p = ch; else
    if(ch==0x401) *p = 0xF0; else
    if(ch<0x410) *p = '?'; else
    if(ch<0x440) *p = ch-0x410+0x80; else
    if(ch<0x450) *p = ch-0x440+0xE0; else
    if(ch==0x451) *p = 0xF1; else
    *p='?';
    return 1;
}

DWORD ic_get_utf8(BYTE** p)
{
    DWORD ch = *(*p)++,x=ch; if(ch<0x80) return ch;
    if(ch<0xC0||ch>=0xF8) return '?'; else ch &= 0x1F;
    for(;x&0x40;x<<=1) ch = (ch<<6)|((*(*p)++)&0x3F);
    return ch;
}

DWORD ic_put_utf8(BYTE* p, DWORD ch)
{
    if(ch<0x80) { *p = ch; return 1; }
    if(ch<0x800)  { *p++ = (ch>>6)|0xC0; *p = (ch&0x3F)|0x80; return 2; }
    if(ch<0x10000)  { *p++ = (ch>>12)|0xC0; *p++ = ((ch>>6)&0x3F)|0x80; *p = (ch&0x3F)|0x80; return 3; }
    *p++ = (ch>>18)|0xC0; *p++ = ((ch>>12)&0x3F)|0x80; *p++ = ((ch>>6)&0x3F)|0x80; *p = (ch&0x3F)|0x80;
    return 4;
}

DWORD ic_get_utf16le(BYTE** p)
{
    DWORD ch = *(WORD*)*p; *p += 2;
    return ch;
}

DWORD ic_put_utf16le(BYTE* p, DWORD ch)
{
    *(WORD*)p = ch;
    return 2;
}

static DWORD (*ic_get[])(BYTE**) = {ic_get_cp866, ic_get_cp866, ic_get_utf16le, ic_get_utf8};
static DWORD (*ic_put[])(BYTE*,DWORD) = {ic_put_cp866, ic_put_cp866, ic_put_utf16le, ic_put_utf8};

DWORD k_strlen(BYTE* src, int src_cp)
{
    DWORD len; if(src_cp<0) src_cp = *src==0||*src>3 ? 0 : *src++;
    DWORD (*get)(BYTE**) = ic_get[src_cp&3];
    for(len=0; get(&src); ++len);
    return len;
}

DWORD k_strsize(BYTE* src, int src_cp, int dst_cp)
{
    DWORD ch,size=0; if(src_cp<0) src_cp = *src==0||*src>3 ? 0 : *src++;
    DWORD (*get)(BYTE**) = ic_get[src_cp&3];
    DWORD (*put)(BYTE*,DWORD) = ic_put[(dst_cp<0?src_cp:dst_cp)&3];
    BYTE tmp[8]; for(ch=1;ch;) size += put(tmp, ch=get(&src));
    if(dst_cp<0 && src_cp>0) ++size;
    return size;
}

BYTE* k_strncpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp, DWORD maxlen)
{
    DWORD ch; if(src_cp<0) src_cp = *src==0||*src>3 ? 0 : *src++;
    DWORD (*get)(BYTE**) = ic_get[src_cp&3];
    DWORD (*put)(BYTE*,DWORD) = ic_put[(dst_cp<0?src_cp:dst_cp)&3];
    if(dst_cp<0 && src_cp>0) *dst++ = src_cp;
    for(;maxlen>0 && (ch=get(&src))!=0;--maxlen) dst += put(dst, ch); put(dst, 0);
    return dst;
}

BYTE* k_strcpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp)
{
    return k_strncpy(dst,dst_cp,src,src_cp,-1);
}

BYTE* k_strdup(BYTE* src, int src_cp, int dst_cp)
{
    BYTE* dst = malloc(k_strsize(src,src_cp,dst_cp));
    k_strncpy(dst,dst_cp,src,src_cp,-1);
    return dst;
}
