//
//  DLGBACore.c
//  GBADeltaCore (Delta Link)
//
//  gpSP (libretro API, interpreter only: iOS has no JIT) + the glink link cable. See DLGBACore.h.
//

#include "DLGBACore.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "libretro.h"

// Third-party headers: their style is not ours, keep the warnings for our own code.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wold-style-declaration"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "common.h"   // gpSP: backup_type, gamepak_backup, iwram/ewram, cheat_parse
#include "glink.h"    // the link cable (Delta-link/glink/glink.h, unchanged)
#pragma GCC diagnostic pop

// ---- Hooks the shared gpSP source expects from its frontend (added for the Kindle build; same source on both ends) ----
int fe_render = 1;            // 0 = gpSP skips drawing the frame
int fe_mute = 0;              // 1 = gpSP skips audio mixing (the Kindle has no speaker; Delta always wants sound)
volatile int fe_sram_dirty;   // set by gpSP whenever the game writes its save chip

// ---- State ----
static dl_callbacks dl_cb;
static bool dl_running;
static bool dl_inited;
static int dl_port;
static int dl_last_link = DL_LINK_OFF;
static uint32_t dl_keys;      // GBA KEYINPUT bit order
static uint8_t *dl_target;    // BGRA destination for this frame, or NULL
static int dl_save_countdown; // frames since the last save-chip write, counting down to "write the .sav now"
static unsigned dl_cheat_count;

#define DL_SAVE_SETTLE_FRAMES 60   // ~1 s after the game's last write to flash/SRAM/EEPROM

static void dl_logf(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    size_t n = strlen(line);
    if (n && line[n - 1] == '\n') line[n - 1] = 0;
    if (dl_cb.log) dl_cb.log(line, dl_cb.ctx);
}

// ---- libretro frontend callbacks ----

static void RETRO_CALLCONV dl_retro_log(enum retro_log_level level, const char *fmt, ...)
{
    if (level < RETRO_LOG_WARN) return;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    dl_logf("gpSP: %s", line);
}

static const char *dl_option(const char *key)
{
    static const char *const opts[][2] = {
        { "gpsp_bios",            "builtin"  },   // Delta ships no BIOS files; gpSP's open BIOS is built in
        { "gpsp_boot_mode",       "game"     },
        { "gpsp_drc",             "disabled" },   // iOS: no JIT (and the dynarec is not even compiled in)
        { "gpsp_serial",          "auto"     },   // FireRed/LeafGreen/Emerald -> Wireless Adapter (Union Room)
        { "gpsp_sound_rate",      "32768"    },
        { "gpsp_rtc",             "auto"     },
        { "gpsp_rtc_time_source", "system"   },   // real clock for Ruby/Sapphire/Emerald, like VBA-M did
        { "gpsp_rumble",          "disabled" },
        { "gpsp_frameskip",       "disabled" },   // Delta does its own frame skipping
        { "gpsp_color_correction","disabled" },
        { "gpsp_frame_mixing",    "disabled" },
    };
    for (size_t i = 0; i < sizeof opts / sizeof opts[0]; i++)
        if (!strcmp(key, opts[i][0])) return opts[i][1];
    return NULL;
}

static bool RETRO_CALLCONV dl_environment(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = dl_retro_log;
        return true;

    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;

    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        var->value = dl_option(var->key);
        return var->value != NULL;
    }

    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;

    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;

    case RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE:
        glink_set_core((const struct retro_netpacket_callback *)data);
        return true;

    case RETRO_ENVIRONMENT_SET_MESSAGE: {
        const struct retro_message *msg = (const struct retro_message *)data;
        if (msg && msg->msg) dl_logf("gpSP: %s", msg->msg);
        return true;
    }

    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT: {
        const struct retro_message_ext *msg = (const struct retro_message_ext *)data;
        if (msg && msg->msg) dl_logf("gpSP: %s", msg->msg);
        return true;
    }

    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
        *(int *)data = fe_render ? 3 : 2;   // bit 0 video, bit 1 audio
        return true;

    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        return true;

    default:
        // Core options v1/v2, perf/VFS/rumble interfaces, directories, audio buffer status, fast-forward override:
        // all optional. Saying "no" makes gpSP fall back to its built-in defaults.
        return false;
    }
}

static inline uint32_t dl_565_to_bgra(uint16_t p)
{
    uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;   // little-endian memory order: B G R A
}

static void RETRO_CALLCONV dl_video(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || !dl_target) return;
    if (width > DL_WIDTH) width = DL_WIDTH;
    if (height > DL_HEIGHT) height = DL_HEIGHT;
    for (unsigned y = 0; y < height; y++)
    {
        const uint16_t *src = (const uint16_t *)((const uint8_t *)data + y * pitch);
        uint32_t *dst = (uint32_t *)(dl_target + (size_t)y * DL_WIDTH * 4);
        for (unsigned x = 0; x < width; x++) dst[x] = dl_565_to_bgra(src[x]);
    }
}

static size_t RETRO_CALLCONV dl_audio_batch(const int16_t *frames, size_t count)
{
    if (dl_cb.audio && count) dl_cb.audio(frames, count, dl_cb.ctx);
    return count;
}

static void RETRO_CALLCONV dl_audio_sample(int16_t left, int16_t right)
{
    int16_t s[2] = { left, right };
    dl_audio_batch(s, 1);
}

static void RETRO_CALLCONV dl_input_poll(void) { }

static int16_t RETRO_CALLCONV dl_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;

    // GBA KEYINPUT bit -> RetroPad button
    static const struct { uint32_t gba; unsigned pad; } map[] = {
        { 1u << 0, RETRO_DEVICE_ID_JOYPAD_A },     { 1u << 1, RETRO_DEVICE_ID_JOYPAD_B },
        { 1u << 2, RETRO_DEVICE_ID_JOYPAD_SELECT },{ 1u << 3, RETRO_DEVICE_ID_JOYPAD_START },
        { 1u << 4, RETRO_DEVICE_ID_JOYPAD_RIGHT }, { 1u << 5, RETRO_DEVICE_ID_JOYPAD_LEFT },
        { 1u << 6, RETRO_DEVICE_ID_JOYPAD_UP },    { 1u << 7, RETRO_DEVICE_ID_JOYPAD_DOWN },
        { 1u << 8, RETRO_DEVICE_ID_JOYPAD_R },     { 1u << 9, RETRO_DEVICE_ID_JOYPAD_L },
    };
    uint32_t keys = dl_keys, mask = 0;
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (keys & map[i].gba) mask |= 1u << map[i].pad;

    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)mask;
    return (id < 16 && (mask >> id) & 1) ? 1 : 0;
}

// ---- Link ----

static void dl_link_report(void)
{
    int s = glink_state();
    if (s == dl_last_link) return;
    dl_last_link = s;
    if (dl_cb.link) dl_cb.link(s, s == GLINK_LINKED ? "" : glink_error(), dl_cb.ctx);
}

static void dl_link_open(void)
{
    if (dl_port <= 0) return;
    if (glink_host(dl_port) != 0)
    {
        glink_g.state = GLINK_OFF;
        snprintf(glink_g.err, sizeof glink_g.err, "%s", "could not listen for the Kindle");
        dl_logf("Delta Link: could not listen on port %d (%s)", dl_port, strerror(errno));
    }
    else dl_logf("Delta Link: waiting for the Kindle on port %d", dl_port);
}

// Close and re-open the listener. `why` (may be NULL) is kept as the error the next report carries.
static void dl_link_reopen(const char *why)
{
    char reason[sizeof glink_g.err];
    snprintf(reason, sizeof reason, "%s", why ? why : "");
    glink_close();
    dl_link_open();
    if (reason[0] && !glink_g.err[0]) snprintf(glink_g.err, sizeof glink_g.err, "%s", reason);
    dl_link_report();
}

void dl_link_restart(void)
{
    if (!dl_running || dl_port <= 0) return;
    dl_link_reopen(NULL);
}

// After a drop, open a fresh listener so the Kindle can simply reconnect (and so a socket that died while the
// app was suspended does not leave us accepting on a dead fd).
static void dl_link_check(void)
{
    if (dl_last_link == DL_LINK_LINKED && glink_state() != GLINK_LINKED) dl_link_reopen(glink_error());
    else dl_link_report();
}

int dl_link_state(void) { return glink_state(); }
const char *dl_link_error(void) { return glink_error(); }

// ---- Lifecycle ----

bool dl_start(const char *rom_path, const dl_callbacks *callbacks, int link_port)
{
    if (dl_running) dl_stop();

    memset(&dl_cb, 0, sizeof dl_cb);
    if (callbacks) dl_cb = *callbacks;
    dl_keys = 0;
    dl_target = NULL;
    dl_save_countdown = 0;
    dl_cheat_count = 0;
    fe_render = 1;
    fe_mute = 0;
    fe_sram_dirty = 0;

    retro_set_environment(dl_environment);
    retro_set_video_refresh(dl_video);
    retro_set_audio_sample(dl_audio_sample);
    retro_set_audio_sample_batch(dl_audio_batch);
    retro_set_input_poll(dl_input_poll);
    retro_set_input_state(dl_input_state);
    retro_init();
    dl_inited = true;

    struct retro_game_info info;
    memset(&info, 0, sizeof info);
    info.path = rom_path;   // gpSP reads the file itself (need_fullpath)
    if (!retro_load_game(&info))
    {
        dl_logf("Delta Link: gpSP could not load %s", rom_path ? rom_path : "(null)");
        retro_deinit();
        dl_inited = false;
        return false;
    }

    dl_running = true;
    fe_sram_dirty = 0;   // loading/resetting is not the game writing its save

    dl_port = link_port;
    dl_last_link = DL_LINK_OFF;
    dl_link_open();
    dl_link_report();
    return true;
}

void dl_stop(void)
{
    if (dl_running)
    {
        glink_close();
        dl_last_link = DL_LINK_OFF;   // no "link lost" banner for a game the player closed
        retro_unload_game();
    }
    if (dl_inited) retro_deinit();
    dl_running = false;
    dl_inited = false;
    dl_port = 0;
    dl_target = NULL;
}

bool dl_is_running(void) { return dl_running; }

bool dl_run_frame(uint8_t *bgra)
{
    if (!dl_running) return false;

    glink_pump();
    dl_link_check();

    dl_target = bgra;
    fe_render = bgra != NULL;
    retro_run();   // gpSP may also poll the link from inside the frame
    dl_target = NULL;

    dl_link_check();

    if (fe_sram_dirty)
    {
        fe_sram_dirty = 0;
        dl_save_countdown = DL_SAVE_SETTLE_FRAMES;
    }
    else if (dl_save_countdown > 0 && --dl_save_countdown == 0)
    {
        return true;
    }
    return false;
}

void dl_set_keys(uint32_t keys) { dl_keys = keys; }

double dl_frames_per_second(void)
{
    struct retro_system_av_info av;
    memset(&av, 0, sizeof av);
    retro_get_system_av_info(&av);
    return av.timing.fps > 1.0 ? av.timing.fps : 59.7275;
}

// ---- Saves ----

size_t dl_battery_size(void)
{
    switch (backup_type)
    {
    case BACKUP_FLASH:  return flash_bank_cnt == FLASH_SIZE_128KB ? 0x20000 : 0x10000;
    case BACKUP_SRAM:   return 0x8000;
    case BACKUP_EEPROM: return eeprom_size == EEPROM_8_KBYTE ? 0x2000 : 0x200;
    default:            return 0;
    }
}

bool dl_load_battery(const char *path)
{
    if (!dl_running || !path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    uint8_t *ram = (uint8_t *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    size_t cap = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!ram || !buf) { fclose(f); free(buf); return false; }

    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    if (n == 0) { free(buf); return false; }

    memset(ram, 0xFF, cap);   // erased flash/EEPROM reads 0xFF
    memcpy(ram, buf, n);
    free(buf);

    // A 512-byte EEPROM dump means a 4 Kbit chip; anything bigger is the 64 Kbit one.
    if (backup_type == BACKUP_EEPROM && n > 0x200) eeprom_size = EEPROM_8_KBYTE;

    fe_sram_dirty = 0;
    dl_save_countdown = 0;
    return true;
}

bool dl_save_battery(const char *path)
{
    if (!dl_running || !path) return false;
    size_t size = dl_battery_size();
    if (size == 0) return false;   // no save chip in use (yet): leave any existing file alone

    const uint8_t *ram = (const uint8_t *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!ram) return false;

    char tmp[4096];
    if (snprintf(tmp, sizeof tmp, "%s.dltmp", path) >= (int)sizeof tmp) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(ram, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp, path) != 0) { remove(tmp); return false; }
    return true;
}

bool dl_save_state(const char *path)
{
    if (!dl_running || !path) return false;
    size_t size = retro_serialize_size();
    void *buf = malloc(size);
    if (!buf) return false;
    bool ok = retro_serialize(buf, size);
    if (ok)
    {
        FILE *f = fopen(path, "wb");
        ok = f && fwrite(buf, 1, size, f) == size;
        if (f) ok = (fclose(f) == 0) && ok;
    }
    free(buf);
    return ok;
}

bool dl_load_state(const char *path)
{
    if (!dl_running || !path) return false;
    size_t size = retro_serialize_size();
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    void *buf = malloc(size);
    bool ok = buf && fread(buf, 1, size, f) == size;
    fclose(f);
    if (ok) ok = retro_unserialize(buf, size);
    else dl_logf("Delta Link: %s is not a gpSP save state", path);
    free(buf);
    return ok;
}

// ---- Cheats ----

void dl_cheats_reset(void)
{
    retro_cheat_reset();
    dl_cheat_count = 0;
}

bool dl_cheat_add(const char *code)
{
    if (!code || dl_cheat_count >= MAX_CHEATS) return false;
    if (cheat_parse(dl_cheat_count, code) != CheatNoError)
    {
        cheat_disable(dl_cheat_count);
        return false;
    }
    dl_cheat_count++;
    return true;
}

// ---- Memory ----

const uint8_t *dl_memory(long address, long size)
{
    if (address < 0 || size < 0) return NULL;
    if (address >= 0x8000)
    {
        if (address - 0x8000 + size > 0x40000) return NULL;
        return ewram + (address - 0x8000);
    }
    if (address + size > 0x8000) return NULL;
    return iwram + 0x8000 + address;
}
