#pragma pack(push,1)
typedef struct
{
    BYTE magic[4];
    union
    {
        struct
        {
            BYTE magic[4];
            DWORD version;
            DWORD start;
            DWORD code_size;
            DWORD ram_size;
            DWORD stack_pos;
            DWORD args_buf;
            DWORD path_buf;
        } menuet;
        struct
        {
            DWORD unpacked;
            DWORD flags;
        } kpck;
    } u;
} KEX_FILE_HDR;

typedef struct
{
    WORD machine;
    WORD nSection;
    DWORD DataTime;
    DWORD pSymTable;
    DWORD nSymbols;
    WORD optHeader;
    WORD flags;
} COFF_HEADER;

typedef struct
{
    char name[8];
    DWORD VirtualSize;
    DWORD VirtualAddress;
    DWORD SizeOfRawData;
    DWORD PtrRawData;
    DWORD PtrReloc;
    DWORD PtrLineNum;
    WORD NumReloc;
    WORD NumLineNum;
    DWORD Flags;
} COFF_SECTION;

typedef struct
{
    DWORD VirtualAddress;
    DWORD SymIndex;
    WORD Type;
} COFF_RELOC;

typedef struct
{
    char name[8];
    DWORD Value;
    WORD SectionNumber;
    WORD Type;
    BYTE StorageClass;
    BYTE MaxAuxSymbols;
} COFF_SYM;
#pragma pack(pop)

void k_proc_init();
void k_exec(k_context* ctx, char* kexfile, char* args);
void k_start_thread(DWORD eip, DWORD esp);
void* k_load(k_context* ctx, BYTE* name, int cp, void*(*_mem_alloc)(DWORD), DWORD* exports, DWORD* psize);
DWORD k_load_file(k_context* ctx, BYTE* name, int cp, DWORD* psize);
DWORD k_load_dll(k_context* ctx, BYTE* name, int cp);
