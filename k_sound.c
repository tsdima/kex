#include "k_mem.h"
#include "k_sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>

#include <pulse/simple.h>
#include <pulse/error.h>

// PC-speaker emulation for KolibriOS mcall 55 sub 55.
// Kolibri melody format (from kernel/trunk/sound/playnote.inc):
//   byte 0x00           = end of data
//   byte 0x01..0x80     = duration in 1/100 sec; next 2 bytes = 8253 divider
//                         (freq = 1193180/divider)
//   byte 0x81           = invalid (terminator)
//   byte 0x82..0xFF     = compact: duration = firstbyte - 0x81;
//                         next byte = 0xFF (silence) or octave<<4 | note (1..12)
// Semitone dividers for the lowest octave (Kolibri "octave 0" = C2..B2):
static const WORD kontrOctave[12] = {
    0x4742, 0x4342, 0x3F7C, 0x3BEC, 0x388F, 0x3562,
    0x3264, 0x2F8F, 0x2CE4, 0x2A5F, 0x2802, 0x25BF
};

#define SR 48000
#define AMP 4800
#define MELODY_MAX 4096
#define QUEUE_LEN 8

static BYTE* queue[QUEUE_LEN];
static int q_head, q_tail;
static pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  q_cond  = PTHREAD_COND_INITIALIZER;
static pa_simple* pa_stream = NULL;
static int pa_tried = 0;

static void synth_square(short* buf, int nsamples, int freq)
{
    if (freq <= 0 || nsamples <= 0) { memset(buf, 0, nsamples*2); return; }
    int period = SR / freq; if (period < 2) period = 2;
    int half = period / 2;
    for (int i = 0; i < nsamples; ++i) buf[i] = ((i % period) < half) ? AMP : -AMP;

    int fade = SR / 200; // ~5ms fade to suppress clicks between notes
    if (fade > nsamples / 2) fade = nsamples / 2;
    for (int i = 0; i < fade; ++i) {
        buf[i]              = (short)(buf[i]              * i / fade);
        buf[nsamples-1-i]   = (short)(buf[nsamples-1-i]   * i / fade);
    }
}

static void play_note(pa_simple* pa, int divider, int duration_hundredths)
{
    int freq = divider > 0 ? (1193180 / divider) : 0;
    int nsamples = (SR * duration_hundredths) / 100;
    if (nsamples <= 0) return;
    short* buf = malloc(nsamples * 2); if (!buf) return;
    synth_square(buf, nsamples, freq);
    pa_simple_write(pa, buf, nsamples*2, NULL);
    free(buf);
}

static void play_melody(pa_simple* pa, const BYTE* data, int len)
{
    int i = 0;
    while (i < len) {
        BYTE b = data[i++];
        if (b == 0 || b == 0x81) break;
        int duration, divider;
        if (b <= 0x80) {
            if (i + 2 > len) break;
            duration = b;
            divider  = data[i] | (data[i+1] << 8);
            i += 2;
        } else {
            if (i + 1 > len) break;
            duration = b - 0x81;
            BYTE code = data[i++];
            if (code == 0xFF) divider = 0;
            else {
                int octave = code >> 4;
                int note   = (code & 0xF) - 1;
                if (note < 0 || note >= 12) continue;
                divider = kontrOctave[note] >> octave;
                if (divider == 0) divider = 1;
            }
        }
        play_note(pa, divider, duration);
    }
    pa_simple_drain(pa, NULL);
}

static int melody_length(const BYTE* data)
{
    int i = 0;
    while (i < MELODY_MAX) {
        BYTE b = data[i++];
        if (b == 0 || b == 0x81) return i;
        if (b <= 0x80) { i += 2; if (i > MELODY_MAX) break; }
        else           { i += 1; if (i > MELODY_MAX) break; }
    }
    return i > MELODY_MAX ? MELODY_MAX : i;
}

static void* sound_thread_fn(void* arg)
{
    (void)arg;
    for (;;) {
        BYTE* data;
        pthread_mutex_lock(&q_mutex);
        while (q_head == q_tail) pthread_cond_wait(&q_cond, &q_mutex);
        data = queue[q_head]; queue[q_head] = NULL;
        q_head = (q_head + 1) % QUEUE_LEN;
        pthread_mutex_unlock(&q_mutex);

        if (!pa_stream && !pa_tried) {
            pa_tried = 1;
            pa_sample_spec ss;
            ss.format = PA_SAMPLE_S16LE; ss.rate = SR; ss.channels = 1;
            int err = 0;
            pa_stream = pa_simple_new(NULL, "kex", PA_STREAM_PLAYBACK, NULL,
                                     "PC speaker", &ss, NULL, NULL, &err);
            if (!pa_stream) fprintf(stderr, "kex sound: pa_simple_new failed: %s\n", pa_strerror(err));
        }
        if (pa_stream) play_melody(pa_stream, data, melody_length(data));
        free(data);
    }
    return NULL;
}

void k_sound_init(void)
{
    pthread_t th;
    if (pthread_create(&th, NULL, sound_thread_fn, NULL) == 0) pthread_detach(th);
}

DWORD k_speaker_play(BYTE* data)
{
    int len = melody_length(data);
    BYTE* copy = malloc(len);
    if (!copy) return 55;
    memcpy(copy, data, len);

    pthread_mutex_lock(&q_mutex);
    int next = (q_tail + 1) % QUEUE_LEN;
    if (next == q_head) {
        pthread_mutex_unlock(&q_mutex);
        free(copy);
        return 55; // busy
    }
    queue[q_tail] = copy;
    q_tail = next;
    pthread_cond_signal(&q_cond);
    pthread_mutex_unlock(&q_mutex);
    return 0;
}

// -------- INFINITY / SOUND driver emulation --------------------------------
// Codes and format constants come from drivers/audio/infinity/main.inc.
#define PCM_OUT     0x08000000
#define PCM_RING    0x10000000
#define PCM_STATIC  0x20000000

#define SRV_GETVERSION    0
#define SND_CREATE_BUFF   1
#define SND_DESTROY_BUFF  2
#define SND_SETFORMAT     3
#define SND_GETFORMAT     4
#define SND_RESET         5
#define SND_SETPOS        6
#define SND_GETPOS        7
#define SND_SETBUFF       8
#define SND_OUT           9
#define SND_PLAY          10
#define SND_STOP          11
#define SND_SETVOLUME     12
#define SND_GETVOLUME     13
#define SND_SETPAN        14
#define SND_GETPAN        15
#define SND_GETBUFFSIZE   16
#define SND_GETFREESPACE  17

#define DEV_GET_MASTERVOL 7
#define DEV_SET_MASTERVOL 6
#define DEV_GET_INFO      8

#define MAX_STREAMS 4
// Ring size: ~1 sec of stereo 16-bit 48kHz = 192 KB; use 512 KB for headroom.
#define RING_BYTES  (512 * 1024)

typedef struct {
    int in_use;
    int channels;
    int bits;
    int rate;
    DWORD flags;        // PCM_OUT / RING / STATIC
    pa_simple* pa;
    pthread_t thr;
    volatile int stop;
    volatile int playing;
    BYTE* ring;         // ring buffer
    DWORD ring_size;    // bytes
    DWORD wp, rp;       // monotonic byte counters
    pthread_mutex_t mtx;
    pthread_cond_t not_empty;
    int master_lvol_pct; // 0..100 (from Kolibri -0x2710..0 → we clamp)
    int master_rvol_pct;
} k_stream;

static k_stream streams[MAX_STREAMS];
static pthread_mutex_t streams_mtx = PTHREAD_MUTEX_INITIALIZER;
static int master_vol_kolibri = 0; // 0 = full, -0x2710 = silence

static int decode_pcm_format(DWORD fmt, int* ch, int* bits, int* rate)
{
    static const int rates[9] = { 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000 };
    int v = fmt & 0xFF;
    if (v < 1 || v > 36) return -1;
    v -= 1;
    *bits = (v < 18) ? 16 : 8;
    v %= 18;
    *rate = rates[v / 2];
    *ch   = (v % 2 == 0) ? 2 : 1;
    return 0;
}

static void* stream_thread(void* arg)
{
    k_stream* s = arg;
    BYTE scratch[8192];
    for (;;) {
        pthread_mutex_lock(&s->mtx);
        while (!s->stop && (!s->playing || s->wp == s->rp))
            pthread_cond_wait(&s->not_empty, &s->mtx);
        if (s->stop) { pthread_mutex_unlock(&s->mtx); break; }

        DWORD have = s->wp - s->rp;
        DWORD off  = s->rp % s->ring_size;
        DWORD n    = have < sizeof(scratch) ? have : sizeof(scratch);
        if (off + n > s->ring_size) n = s->ring_size - off;
        memcpy(scratch, s->ring + off, n);
        s->rp += n;
        pthread_mutex_unlock(&s->mtx);

        if (s->pa) pa_simple_write(s->pa, scratch, n, NULL);
    }
    return NULL;
}

static int stream_open_pa(k_stream* s)
{
    pa_sample_spec ss;
    ss.format   = (s->bits == 8) ? PA_SAMPLE_U8 : PA_SAMPLE_S16LE;
    ss.rate     = s->rate;
    ss.channels = s->channels;
    int err = 0;
    s->pa = pa_simple_new(NULL, "kex", PA_STREAM_PLAYBACK, NULL,
                          "INFINITY", &ss, NULL, NULL, &err);
    if (!s->pa) {
        fprintf(stderr, "kex sound: pa_simple_new (INFINITY) failed: %s\n", pa_strerror(err));
        return -1;
    }
    return 0;
}

static void stream_free(k_stream* s)
{
    if (!s->in_use) return;
    pthread_mutex_lock(&s->mtx);
    s->stop = 1; s->playing = 0;
    pthread_cond_signal(&s->not_empty);
    pthread_mutex_unlock(&s->mtx);
    pthread_join(s->thr, NULL);
    if (s->pa) { pa_simple_free(s->pa); s->pa = NULL; }
    free(s->ring); s->ring = NULL;
    pthread_mutex_destroy(&s->mtx);
    pthread_cond_destroy(&s->not_empty);
    memset(s, 0, sizeof(*s));
}

DWORD k_infinity_ioctl(DWORD code, void* idata, DWORD ilen, void* odata, DWORD olen)
{
    DWORD* in = idata;
    DWORD* out = odata;

    if (code == SRV_GETVERSION) {
        if (olen >= 4) *out = 5;
        return 0;
    }

    if (code == SND_CREATE_BUFF) {
        if (ilen < 8 || olen < 4) return -1;
        DWORD format = in[0];
        int ch, bits, rate;
        if (decode_pcm_format(format, &ch, &bits, &rate) < 0) return -1;

        pthread_mutex_lock(&streams_mtx);
        int idx = -1;
        for (int i = 0; i < MAX_STREAMS; ++i) if (!streams[i].in_use) { idx = i; break; }
        if (idx < 0) { pthread_mutex_unlock(&streams_mtx); return -1; }
        k_stream* s = &streams[idx];
        memset(s, 0, sizeof(*s));
        s->in_use   = 1;
        s->channels = ch;
        s->bits     = bits;
        s->rate     = rate;
        s->flags    = format & 0xFF000000;
        s->ring     = malloc(RING_BYTES);
        s->ring_size = RING_BYTES;
        pthread_mutex_init(&s->mtx, NULL);
        pthread_cond_init(&s->not_empty, NULL);
        if (!s->ring || stream_open_pa(s) < 0) {
            stream_free(s);
            pthread_mutex_unlock(&streams_mtx);
            return -1;
        }
        pthread_create(&s->thr, NULL, stream_thread, s);
        *out = idx + 1; // handle: 1-based
        pthread_mutex_unlock(&streams_mtx);
        return 0;
    }

    // All remaining codes take the stream handle as the first input DWORD.
    if (ilen < 4) return -1;
    int idx = in[0] - 1;
    if (idx < 0 || idx >= MAX_STREAMS || !streams[idx].in_use) return -1;
    k_stream* s = &streams[idx];

    switch (code) {
    case SND_DESTROY_BUFF:
        pthread_mutex_lock(&streams_mtx);
        stream_free(s);
        pthread_mutex_unlock(&streams_mtx);
        return 0;

    case SND_SETFORMAT: {
        if (ilen < 8) return -1;
        int ch, bits, rate;
        if (decode_pcm_format(in[1], &ch, &bits, &rate) < 0) return -1;
        if (ch != s->channels || bits != s->bits || rate != s->rate) {
            // Re-open PA stream with new format.
            pthread_mutex_lock(&s->mtx);
            if (s->pa) { pa_simple_free(s->pa); s->pa = NULL; }
            s->channels = ch; s->bits = bits; s->rate = rate;
            stream_open_pa(s);
            pthread_mutex_unlock(&s->mtx);
        }
        return 0;
    }

    case SND_RESET:
    case SND_STOP:
        pthread_mutex_lock(&s->mtx);
        s->rp = s->wp = 0;
        s->playing = 0;
        pthread_mutex_unlock(&s->mtx);
        return 0;

    case SND_SETBUFF:
    case SND_OUT:
        // These codes carry a secondary Kolibri virtual pointer (`src`) that
        // the generic driver dispatch can't translate. do_driver_ioctl calls
        // k_infinity_feed() before us for those two codes; here we just ack.
        return 0;

    case SND_PLAY:
        pthread_mutex_lock(&s->mtx);
        s->playing = 1;
        pthread_cond_signal(&s->not_empty);
        pthread_mutex_unlock(&s->mtx);
        return 0;

    case SND_SETVOLUME:
        if (ilen >= 12) { s->master_lvol_pct = in[1]; s->master_rvol_pct = in[2]; }
        return 0;

    case SND_GETVOLUME:
        if (olen >= 8) { out[0] = s->master_lvol_pct; out[1] = s->master_rvol_pct; }
        return 0;

    case SND_GETBUFFSIZE:
        if (olen >= 4) *out = s->ring_size;
        return 0;

    case SND_GETFREESPACE:
        if (olen >= 4) {
            pthread_mutex_lock(&s->mtx);
            DWORD used = s->wp - s->rp;
            *out = used < s->ring_size ? s->ring_size - used : 0;
            pthread_mutex_unlock(&s->mtx);
        }
        return 0;

    case SND_GETPOS:
        if (olen >= 4) *out = (s->wp) & 0xFFFFFFFF;
        return 0;

    default:
        return -1;
    }
}

DWORD k_infinity_feed(DWORD code, BYTE* app_base, DWORD app_size, DWORD* args)
{
    if (code != SND_OUT && code != SND_SETBUFF) return 0;
    DWORD handle = args[0];
    int idx = handle - 1;
    if (idx < 0 || idx >= MAX_STREAMS || !streams[idx].in_use) return -1;
    k_stream* s = &streams[idx];

    DWORD src, size;
    if (code == SND_OUT) { src = args[1]; size = args[2]; }
    else /* SND_SETBUFF */ {
        // {stream, src, offs, size} — write at absolute offset into ring
        src = args[1];
        DWORD offs = args[2];
        size = args[3];
        pthread_mutex_lock(&s->mtx);
        if (offs + size > s->ring_size) size = s->ring_size - (offs % s->ring_size);
        if (src + size <= app_size) memcpy(s->ring + (offs % s->ring_size), app_base + src, size);
        pthread_mutex_unlock(&s->mtx);
        return 0;
    }

    if (src >= app_size || src + size > app_size) return -1;

    // Streaming append: block until there's room in the ring, up to a
    // reasonable timeout so we don't deadlock a broken app.
    DWORD remaining = size, off_src = 0;
    while (remaining > 0) {
        pthread_mutex_lock(&s->mtx);
        DWORD free_bytes = s->ring_size - (s->wp - s->rp);
        DWORD n = remaining < free_bytes ? remaining : free_bytes;
        if (n == 0) {
            pthread_mutex_unlock(&s->mtx);
            // wait for consumer to make room
            struct timespec ts = { 0, 5 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            continue;
        }
        DWORD off_ring = s->wp % s->ring_size;
        if (off_ring + n > s->ring_size) n = s->ring_size - off_ring;
        memcpy(s->ring + off_ring, app_base + src + off_src, n);
        s->wp += n;
        pthread_cond_signal(&s->not_empty);
        pthread_mutex_unlock(&s->mtx);
        off_src   += n;
        remaining -= n;
    }
    return 0;
}

DWORD k_sound_ioctl(DWORD code, void* idata, DWORD ilen, void* odata, DWORD olen)
{
    switch (code) {
    case SRV_GETVERSION:
        if (olen >= 4) *(DWORD*)odata = 1;
        return 0;
    case DEV_GET_MASTERVOL:
        if (olen >= 4) *(DWORD*)odata = master_vol_kolibri;
        return 0;
    case DEV_SET_MASTERVOL:
        if (ilen >= 4) master_vol_kolibri = *(DWORD*)idata;
        return 0;
    case DEV_GET_INFO:
        // Minimal 9*DWORD CTRL_INFO. Zero everything -> "unknown hardware".
        if (olen >= 36) memset(odata, 0, 36);
        return 0;
    default:
        return 0;
    }
}
