#include "psx/libcd.h"

#include "psx/libetc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The disc is served from the assets folder (tools/make_assets.py). Files found with
 * CdSearchFile get a place on a virtual disc, so the game's sector arithmetic keeps working:
 * every read at a file's sectors comes from that file. CD audio tracks are music/trackNN.wav,
 * placed after the data with their 2-second pregaps; playing them reads the WAV files. */

#define DATA_SECTOR 2048
#define RAW_SECTOR 2352
#define RAW_DATA_OFFSET 24  /* Mode 2 Form 1 data inside a raw sector */
#define LEAD_IN 150         /* the first sector of track 1 is at 00:02:00 */
#define FIRST_FILE_SECTOR (LEAD_IN + 0x1000)
#define MAX_FILES 32
#define MAX_TRACKS 99
#define WAV_HEADER 44
#define AUDIO_PREGAP 150    /* 2 seconds between tracks, as on the disc */
#define FRAMES_PER_SECTOR 588 /* 44.1 kHz stereo frames in a CD-DA sector */
#define REPORT_INTERVAL 10  /* sectors between position reports */
#define STAT_PLAYING 0x82   /* motor on, playing */

typedef struct {
    char name[32];  /* as searched, without ";1" */
    FILE *file;
    int start;      /* absolute sector */
    int sectors;
    bool raw;       /* .STR/.XA: stored as raw 2352-byte sectors */
} DiscFile;

static char assets_dir[1024];
static DiscFile files[MAX_FILES];
static int file_count;
static int next_free = FIRST_FILE_SECTOR;
static int track_start[MAX_TRACKS + 1]; /* [1] is the data track, audio from [2] */
static int track_sectors[MAX_TRACKS + 1];
static int track_count;
static int disc_end;
static int read_pos;

/* CD-DA play state. The head position counts 44.1 kHz frames from the start of the disc. */
static bool audio_playing, audio_muted, setloc_pending;
static int64_t audio_frame;
static u_char drive_mode;
static CdlATV mix = {0x80, 0, 0x80, 0};
static CdlCB ready_callback;
static FILE *track_file;
static int track_file_number;

static void stop_streaming(void);

static int bcd(int v) { return (v / 10) << 4 | (v % 10); }
static int unbcd(int v) { return (v >> 4) * 10 + (v & 15); }
static int msf_to_int(int m, int s, int f) { return (m * 60 + s) * 75 + f; }

static long file_size(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    return size;
}

static bool has_suffix(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

bool cd_open_assets(const char *dir)
{
    snprintf(assets_dir, sizeof(assets_dir), "%s", dir);
    char path[1200];
    snprintf(path, sizeof(path), "%s/DIRINFO", dir);
    FILE *probe = fopen(path, "rb");
    if (!probe) {
        fprintf(stderr, "no game data in %s (build it with tools/make_assets.py)\n", dir);
        return false;
    }
    fclose(probe);

    /* Audio tracks go after the data area, each preceded by its 2-second pregap. */
    track_start[1] = LEAD_IN;
    track_count = 1;
    int pos = 300000;
    for (int t = 2; t <= MAX_TRACKS; t++) {
        snprintf(path, sizeof(path), "%s/music/track%02d.wav", dir, t);
        FILE *wav = fopen(path, "rb");
        if (!wav)
            break;
        pos += AUDIO_PREGAP;
        track_start[t] = pos;
        track_sectors[t] = (int)((file_size(wav) - WAV_HEADER) / RAW_SECTOR);
        pos += track_sectors[t];
        track_count = t;
        fclose(wav);
    }
    disc_end = pos;
    return true;
}

/* "\DIR\NAME.EXT;1" -> "DIR/NAME.EXT"; a trailing dot ("DIRINFO.") is dropped like the files'. */
static void asset_name(const char *iso, char *out, size_t size)
{
    while (*iso == '\\')
        iso++;
    size_t n = 0;
    for (; *iso && *iso != ';' && n + 1 < size; iso++)
        out[n++] = *iso == '\\' ? '/' : *iso;
    while (n > 0 && out[n - 1] == '.')
        n--;
    out[n] = '\0';
}

static DiscFile *find_sector(int sector)
{
    for (int i = 0; i < file_count; i++)
        if (sector >= files[i].start && sector < files[i].start + files[i].sectors)
            return &files[i];
    return NULL;
}

int CdInit(void)
{
    read_pos = LEAD_IN;
    return 1;
}

CdlFILE *CdSearchFile(CdlFILE *fp, char *name)
{
    char rel[32];
    asset_name(name, rel, sizeof(rel));
    DiscFile *df = NULL;
    for (int i = 0; i < file_count; i++)
        if (strcmp(files[i].name, rel) == 0)
            df = &files[i];
    if (!df) {
        if (file_count == MAX_FILES)
            return NULL;
        char path[1200];
        snprintf(path, sizeof(path), "%s/%s", assets_dir, rel);
        FILE *f = fopen(path, "rb");
        if (!f)
            return NULL;
        df = &files[file_count++];
        snprintf(df->name, sizeof(df->name), "%s", rel);
        df->file = f;
        df->raw = has_suffix(rel, ".STR") || has_suffix(rel, ".XA");
        long size = file_size(f);
        df->sectors = (int)((size + (df->raw ? RAW_SECTOR : DATA_SECTOR) - 1) / (df->raw ? RAW_SECTOR : DATA_SECTOR));
        df->start = next_free;
        next_free += df->sectors + 16;
    }
    CdIntToPos(df->start - LEAD_IN, &fp->pos);
    fp->size = (u_long)df->sectors * DATA_SECTOR; /* the ISO size field counts 2048-byte sectors */
    const char *base = strrchr(rel, '/');
    snprintf(fp->name, sizeof(fp->name), "%s", base ? base + 1 : rel);
    return fp;
}

/* Like Psy-Q, a position given with Play, a seek or a read is sent as a Setloc first. Reading
 * data or seeking stops CD-DA play; Play starts at a pending Setloc, else where the head is. */
int CdControl(u_char com, u_char *param, u_char *result)
{
    (void)result;
    if (param && (com == CdlSetloc || com == CdlPlay || com == CdlSeekL || com == CdlSeekP ||
                  com == CdlReadN || com == CdlReadS)) {
        read_pos = msf_to_int(unbcd(param[0]), unbcd(param[1]), unbcd(param[2]));
        setloc_pending = true;
    }
    switch (com) {
    case CdlPlay:
        if (setloc_pending)
            audio_frame = (int64_t)read_pos * FRAMES_PER_SECTOR;
        setloc_pending = false;
        audio_playing = true;
        break;
    case CdlSeekL:
    case CdlSeekP:
        audio_frame = (int64_t)read_pos * FRAMES_PER_SECTOR;
        setloc_pending = false;
        audio_playing = false;
        break;
    case CdlReadN:
    case CdlReadS:
    case CdlPause:
    case CdlStop:
        audio_playing = false;
        stop_streaming();
        break;
    case CdlMute:
        audio_muted = true;
        break;
    case CdlDemute:
        audio_muted = false;
        break;
    case CdlSetmode:
        if (param)
            drive_mode = param[0];
        break;
    default:
        break;
    }
    return 1;
}

int CdControlB(u_char com, u_char *param, u_char *result)
{
    return CdControl(com, param, result);
}

int CdControlF(u_char com, u_char *param)
{
    return CdControl(com, param, NULL);
}

/* Reads are synchronous: the data is in `buf` when CdRead returns. */
int CdRead(int sectors, u_long *buf, int mode)
{
    (void)mode;
    audio_playing = false;
    stop_streaming();
    u_char *dst = (u_char *)buf;
    for (int i = 0; i < sectors; i++) {
        int s = read_pos + i;
        DiscFile *df = find_sector(s);
        if (!df) {
            fprintf(stderr, "libcd: read of sector %d outside every file\n", s);
            return 0;
        }
        long offset = (long)(s - df->start) * (df->raw ? RAW_SECTOR : DATA_SECTOR) + (df->raw ? RAW_DATA_OFFSET : 0);
        u_char *out = dst + (size_t)i * DATA_SECTOR;
        fseek(df->file, offset, SEEK_SET);
        size_t got = fread(out, 1, DATA_SECTOR, df->file);
        memset(out + got, 0, DATA_SECTOR - got); /* the last sector of a file is padded */
    }
    read_pos += sectors;
    return 1;
}

int CdReadSync(int mode, u_char *result)
{
    (void)mode;
    (void)result;
    return 0;
}

/* loc[0] receives the end of the disc, loc[1..] the track starts; returns the track count. */
int CdGetToc(CdlLOC *loc)
{
    CdIntToPos(disc_end - LEAD_IN, &loc[0]);
    for (int t = 1; t <= track_count; t++) {
        CdIntToPos(track_start[t] - LEAD_IN, &loc[t]);
        loc[t].track = (u_char)bcd(t);
    }
    return track_count;
}

int CdMix(CdlATV *vol)
{
    mix = *vol;
    return 1;
}

CdlCB CdReadyCallback(CdlCB func)
{
    CdlCB old = ready_callback;
    ready_callback = func;
    return old;
}

int CdPosToInt(CdlLOC *p)
{
    return msf_to_int(unbcd(p->minute), unbcd(p->second), unbcd(p->sector)) - LEAD_IN;
}

CdlLOC *CdIntToPos(int i, CdlLOC *p)
{
    i += LEAD_IN;
    p->minute = (u_char)bcd(i / (60 * 75));
    p->second = (u_char)bcd(i / 75 % 60);
    p->sector = (u_char)bcd(i % 75);
    return p;
}

/* The audio track whose data or pregap holds `sector` (0: none), and whether it is data. */
static int audio_track_at(int sector, bool *in_data)
{
    for (int t = track_count; t >= 2; t--) {
        if (sector >= track_start[t] - AUDIO_PREGAP) {
            *in_data = sector >= track_start[t] && sector < track_start[t] + track_sectors[t];
            return t;
        }
    }
    *in_data = false;
    return 0;
}

static bool open_track(int t)
{
    if (track_file_number == t)
        return track_file != NULL;
    if (track_file)
        fclose(track_file);
    char path[1200];
    snprintf(path, sizeof(path), "%s/music/track%02d.wav", assets_dir, t);
    track_file = fopen(path, "rb");
    track_file_number = t;
    return track_file != NULL;
}

static short saturate(int v)
{
    return (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* ---- streaming: CdRead2 and the ring ---- */

#define STR_HEADER 32       /* the sector header at the start of a video sector's data */
#define STR_PAYLOAD (DATA_SECTOR - STR_HEADER)
#define STR_PADDING 64      /* zeros after a frame, so the bitstream reader may look ahead */
#define STREAM_SLOTS 32
#define SUBMODE_AUDIO 0x04
#define SUBMODE_FORM2 0x20
#define XA_FIFO 0x8000      /* stereo samples */

typedef struct {
    bool used, complete;
    unsigned sequence;      /* order of completion */
    u_long header[STR_HEADER / 4];
    u_long *data;
    u_long frame;
    int chunks, received;
} StreamFrame;

static bool streaming;
static long stream_mode;
static int64_t stream_clock;        /* 44.1 kHz frames times sectors a second, in units of 44100 */
static u_long ring_sectors = 32;
static u_long stream_first = 1, stream_last = 0xffffffff;
static StreamFrame stream_frames[STREAM_SLOTS];
static StreamFrame *building;
static unsigned next_sequence;

static int16_t xa_fifo[XA_FIFO][2];
static int xa_head, xa_count;
static int xa_rate = 37800;
static int32_t xa_history[2][2];
static int xa_step;                 /* 0..44099: where the output is between two XA samples */
static int16_t xa_from[2], xa_to[2];

static uint32_t get_le32(const u_char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void free_frame(StreamFrame *f)
{
    free(f->data);
    memset(f, 0, sizeof(*f));
}

static void abandon_building(void)
{
    if (building)
        free_frame(building);
    building = NULL;
}

static u_long ring_used(void)
{
    u_long n = 0;
    for (int i = 0; i < STREAM_SLOTS; i++)
        if (stream_frames[i].used)
            n += (u_long)stream_frames[i].chunks;
    return n;
}

/* A video sector: its chunk goes into its frame; a frame that does not fit in the ring is lost, as
 * on the hardware when the program falls behind. */
static void stream_video_sector(const u_char *data)
{
    if ((data[0] | data[1] << 8) != 0x0160 || (data[2] | data[3] << 8) != 0x8001)
        return;
    int chunk = data[4] | data[5] << 8, chunks = data[6] | data[7] << 8;
    u_long frame = get_le32(data + 8);
    if (frame < stream_first || frame > stream_last || chunks <= 0)
        return;
    if (chunk == 0) {
        abandon_building();
        if (ring_used() + (u_long)chunks > ring_sectors)
            return;
        for (int i = 0; i < STREAM_SLOTS && !building; i++)
            if (!stream_frames[i].used)
                building = &stream_frames[i];
        if (!building)
            return;
        building->used = true;
        building->frame = frame;
        building->chunks = chunks;
        building->data = calloc(1, (size_t)chunks * STR_PAYLOAD + STR_PADDING);
        for (int i = 0; i < STR_HEADER / 4; i++)
            building->header[i] = get_le32(data + i * 4);
    }
    if (!building || building->frame != frame || building->received != chunk) {
        abandon_building();
        return;
    }
    memcpy((u_char *)building->data + (size_t)chunk * STR_PAYLOAD, data + STR_HEADER, STR_PAYLOAD);
    if (++building->received == building->chunks) {
        building->complete = true;
        building->sequence = next_sequence++;
        building = NULL;
    }
}

static int16_t clamp16(int32_t v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

/* An XA ADPCM sector: 18 groups of 128 bytes, each 16 bytes of parameters and 112 of samples in 4 or 8
 * units of 28 (left and right alternating in stereo). */
static void xa_decode_sector(const u_char *raw)
{
    static const int k0[4] = {0, 60, 115, 98}, k1[4] = {0, 0, -52, -55};
    u_char coding = raw[19];
    bool stereo = coding & 1, eight_bit = coding & 0x10;
    int units = eight_bit ? 4 : 8;
    xa_rate = (coding & 4) ? 18900 : 37800;
    const u_char *data = raw + 24;
    for (int g = 0; g < 18; g++) {
        const u_char *group = data + g * 128;
        int16_t out[2][224];
        int count[2] = {0, 0};
        for (int u = 0; u < units; u++) {
            int param = group[4 + u];
            int shift = param & 0xf, filter = (param >> 4) & 3;
            if (shift > 12)
                shift = 9;
            int ch = stereo ? (u & 1) : 0;
            int32_t *h = xa_history[ch];
            for (int j = 0; j < 28; j++) {
                int32_t t;
                if (eight_bit) {
                    t = (int32_t)(int16_t)(group[16 + j * 4 + u] << 8);
                } else {
                    int b = group[16 + j * 4 + u / 2];
                    t = (int32_t)(int16_t)(((u & 1) ? b >> 4 : b & 0xf) << 12);
                }
                int32_t s = clamp16((t >> shift) + ((h[0] * k0[filter] + h[1] * k1[filter] + 32) >> 6));
                h[1] = h[0];
                h[0] = s;
                out[ch][count[ch]++] = (int16_t)s;
            }
        }
        for (int i = 0; i < count[0] && xa_count < XA_FIFO; i++) {
            int at = (xa_head + xa_count++) % XA_FIFO;
            xa_fifo[at][0] = out[0][i];
            xa_fifo[at][1] = stereo ? out[1][i] : out[0][i];
        }
    }
}

static void xa_reset(void)
{
    xa_head = xa_count = 0;
    xa_step = 0;
    memset(xa_history, 0, sizeof(xa_history));
    memset(xa_from, 0, sizeof(xa_from));
    memset(xa_to, 0, sizeof(xa_to));
}

static void stop_streaming(void)
{
    streaming = false;
    abandon_building();
    xa_reset();
}

static void stream_read_sector(void)
{
    DiscFile *df = find_sector(read_pos);
    if (!df || !df->raw) {
        stop_streaming();
        return;
    }
    u_char raw[RAW_SECTOR];
    fseek(df->file, (long)(read_pos - df->start) * RAW_SECTOR, SEEK_SET);
    if (fread(raw, 1, RAW_SECTOR, df->file) != RAW_SECTOR) {
        stop_streaming();
        return;
    }
    read_pos++;
    u_char submode = raw[18];
    if ((submode & SUBMODE_AUDIO) && (submode & SUBMODE_FORM2)) {
        if (stream_mode & CdlModeRT)
            xa_decode_sector(raw);
    } else if (stream_mode & CdlModeStream) {
        stream_video_sector(raw + RAW_DATA_OFFSET);
    }
}

/* The drive reads 75 sectors a second, 150 at double speed. */
static void stream_advance(int frames)
{
    int speed = (stream_mode & CdlModeSpeed) ? 150 : 75;
    stream_clock += (int64_t)frames * speed;
    while (streaming && stream_clock >= 44100) {
        stream_clock -= 44100;
        stream_read_sector();
    }
}

/* XA audio to 44.1 kHz by linear interpolation (the hardware uses a 7-phase filter). */
static void xa_mix(short *stereo, int frames)
{
    for (int i = 0; i < frames; i++) {
        xa_step += xa_rate;
        while (xa_step >= 44100) {
            xa_step -= 44100;
            xa_from[0] = xa_to[0];
            xa_from[1] = xa_to[1];
            if (xa_count > 0) {
                xa_to[0] = xa_fifo[xa_head][0];
                xa_to[1] = xa_fifo[xa_head][1];
                xa_head = (xa_head + 1) % XA_FIFO;
                xa_count--;
            } else {
                xa_to[0] = xa_to[1] = 0;
            }
        }
        if (audio_muted)
            continue;
        int l = xa_from[0] + (xa_to[0] - xa_from[0]) * xa_step / 44100;
        int r = xa_from[1] + (xa_to[1] - xa_from[1]) * xa_step / 44100;
        short *out = &stereo[i * 2];
        out[0] = saturate(out[0] + ((l * mix.val0 + r * mix.val3) >> 7));
        out[1] = saturate(out[1] + ((r * mix.val2 + l * mix.val1) >> 7));
    }
}

void StSetRing(u_long *ring_addr, u_long ring_size)
{
    (void)ring_addr;
    ring_sectors = ring_size;
    StClearRing();
}

void StSetStream(u_long mode, u_long start_frame, u_long end_frame, void (*func1)(void), void (*func2)(void))
{
    (void)mode;
    (void)func1;
    (void)func2;
    stream_first = start_frame;
    stream_last = end_frame;
}

/* The oldest whole frame. While the drive is reading, a miss waits a vblank so it can read on. */
u_long StGetNext(u_long **addr, u_long **header)
{
    StreamFrame *best = NULL;
    for (int i = 0; i < STREAM_SLOTS; i++) {
        StreamFrame *f = &stream_frames[i];
        if (f->complete && (!best || f->sequence < best->sequence))
            best = f;
    }
    if (!best) {
        if (streaming)
            etc_idle();
        return 1;
    }
    best->complete = false; /* handed out; still holds its ring space until StFreeRing */
    *addr = best->data;
    *header = best->header;
    return 0;
}

u_long StFreeRing(u_long *base)
{
    for (int i = 0; i < STREAM_SLOTS; i++) {
        if (stream_frames[i].used && stream_frames[i].data == base) {
            free_frame(&stream_frames[i]);
            return 0;
        }
    }
    return 1;
}

void StClearRing(void)
{
    building = NULL;
    for (int i = 0; i < STREAM_SLOTS; i++)
        if (stream_frames[i].used)
            free_frame(&stream_frames[i]);
}

void StUnSetRing(void)
{
    StClearRing();
}

int CdRead2(long mode)
{
    audio_playing = false;
    stream_mode = mode;
    stream_clock = 0;
    abandon_building();
    xa_reset();
    streaming = true;
    return 1;
}

/* What the drive sends in report mode: status, track, index, absolute time, peak level. Only
 * the absolute-time reports are sent; they are the ones the game reads. */
static void send_report(int sector, int track)
{
    if (!ready_callback || !(drive_mode & CdlModeRept))
        return;
    u_char result[8] = {STAT_PLAYING, (u_char)bcd(track), 1,
                        (u_char)bcd(sector / (60 * 75)), (u_char)bcd(sector / 75 % 60),
                        (u_char)bcd(sector % 75), 0, 0};
    ready_callback(CdlDataReady, result);
}

void cd_audio_read(short *stereo, int frames)
{
    memset(stereo, 0, (size_t)frames * 2 * sizeof(short));
    if (streaming)
        stream_advance(frames);
    int done = 0;
    while (done < frames && audio_playing) {
        int sector = (int)(audio_frame / FRAMES_PER_SECTOR);
        if (sector >= disc_end) {
            audio_playing = false;
            break;
        }
        bool in_data;
        int track = audio_track_at(sector, &in_data);
        int offset = (int)(audio_frame % FRAMES_PER_SECTOR);
        int n = FRAMES_PER_SECTOR - offset;
        if (n > frames - done)
            n = frames - done;
        if (in_data && !audio_muted && open_track(track)) {
            u_char raw[FRAMES_PER_SECTOR * 4];
            long pos = WAV_HEADER + (long)(sector - track_start[track]) * RAW_SECTOR + offset * 4;
            fseek(track_file, pos, SEEK_SET);
            size_t got = fread(raw, 4, (size_t)n, track_file);
            for (size_t i = 0; i < got; i++) {
                int l = (short)(raw[i * 4] | raw[i * 4 + 1] << 8);
                int r = (short)(raw[i * 4 + 2] | raw[i * 4 + 3] << 8);
                short *out = &stereo[(done + (int)i) * 2];
                out[0] = saturate((l * mix.val0 + r * mix.val3) >> 7);
                out[1] = saturate((r * mix.val2 + l * mix.val1) >> 7);
            }
        }
        done += n;
        audio_frame += n;
        int next = (int)(audio_frame / FRAMES_PER_SECTOR);
        if (next != sector && next % REPORT_INTERVAL == 0)
            send_report(next, track);
    }
    xa_mix(stereo, frames);
}
