DWORD k_net_info(k_context* ctx, BYTE devNo, BYTE func, DWORD* ebx, DWORD* ecx);
DWORD k_net_socket(k_context* ctx, BYTE func, DWORD* ebx, DWORD ecx, DWORD edx, DWORD esi, DWORD edi);
DWORD k_net_proto(k_context* ctx, WORD proto, BYTE devNo, BYTE func, DWORD* ebx, DWORD* ecx);
