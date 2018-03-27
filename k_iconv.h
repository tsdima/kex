DWORD k_strlen(BYTE* src, int src_cp);
DWORD k_strsize(BYTE* src, int src_cp, int dst_cp);
BYTE* k_strdup(BYTE* src, int src_cp, int dst_cp);
BYTE* k_strcpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp);
BYTE* k_strncpy(BYTE* dst, int dst_cp, BYTE* src, int src_cp, DWORD maxlen);
