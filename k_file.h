extern char k_root[];

void k_parse_name(k_context* ctx, BYTE* name, int cp, char* buf, int buflen);
DWORD k_file_syscall(k_context* ctx, DWORD* eax, DWORD* ebx, int f80);

void k_copy_path(BYTE* to, BYTE* from);
DWORD k_set_curpath(k_context* ctx, BYTE* path, int cp);
DWORD k_get_curpath(k_context* ctx, BYTE* path, int cp, DWORD len);
DWORD k_set_extfs(BYTE* data);
DWORD k_load_skin(k_context* ctx, BYTE* name);
