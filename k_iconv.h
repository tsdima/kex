#include <wchar.h>

extern BYTE* font9bmp; extern DWORD font9len;
extern BYTE* font16bmp; extern DWORD font16len;

void k_iconv_init();

int   k_strlen(BYTE* p, int cp);
BYTE* k_strdup(BYTE* p, int cp, int to_cp);
BYTE* k_strcpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp);
BYTE* k_strncpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp, DWORD maxlen);

void k_iconv_cp866_to_utf8(char* ip, size_t ilen, char* op, size_t olen);
void k_iconv_utf16_to_utf8(char* ip, size_t ilen, char* op, size_t olen);
