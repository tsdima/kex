#include <wchar.h>

extern BYTE* font9bmp; extern DWORD font9len;
extern BYTE* font16bmp; extern DWORD font16len;

void k_iconv_init();

DWORD k_strlen(BYTE* src, int src_cp);
DWORD k_strsize(BYTE* src, int src_cp, int dst_cp);
BYTE* k_strdup(BYTE* src, int src_cp, int dst_cp);
BYTE* k_strcpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp);
BYTE* k_strncpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp, DWORD maxlen);
