void k_sound_init(void);
DWORD k_speaker_play(BYTE* data);

// Built-in Kolibri driver ioctl handlers. Matches k_driver::ioctl signature
// in k_mem.h so they can be registered directly in do_driver_load().
DWORD k_infinity_ioctl(DWORD code, void* idata, DWORD ilen, void* odata, DWORD olen);
DWORD k_sound_ioctl(DWORD code, void* idata, DWORD ilen, void* odata, DWORD olen);

// Called from do_driver_ioctl for INFINITY when the ioctl carries a
// secondary Kolibri virtual pointer (SND_OUT / SND_SETBUFF), which the
// generic driver dispatch can't translate on its own.
DWORD k_infinity_feed(DWORD code, BYTE* app_base, DWORD app_size, DWORD* args);
