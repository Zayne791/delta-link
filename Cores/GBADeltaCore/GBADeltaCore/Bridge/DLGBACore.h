//
//  DLGBACore.h
//  GBADeltaCore (Delta Link)
//
//  The plain C half of the GBA bridge: drives gpSP through its libretro API and hosts the glink
//  link cable (glink.h) so a second gpSP (the Kindle) can trade over Wi-Fi.
//  No Objective-C in here, so the same file builds and is tested on Linux (see Tests/dl_host_test.c).
//

#ifndef DLGBACORE_H
#define DLGBACORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DL_LINK_OFF = 0, DL_LINK_WAITING = 1, DL_LINK_LINKED = 2 };

#define DL_WIDTH  240
#define DL_HEIGHT 160
#define DL_SAMPLE_RATE 32768   // gpSP is asked for 32768 Hz: the rate GBADeltaCore has always declared to Delta

typedef struct {
    // Interleaved stereo s16 frames, `count` frames (count * 4 bytes).
    void (*audio)(const int16_t *frames, size_t count, void *ctx);
    // The link state changed (DL_LINK_*); `error` is a short reason when a link dropped or failed, else "".
    void (*link)(int state, const char *error, void *ctx);
    // Core log lines (warnings and errors only).
    void (*log)(const char *message, void *ctx);
    void *ctx;
} dl_callbacks;

// Loads the ROM at `path` and starts the core. link_port > 0 also starts hosting the link (player 0) on that TCP port.
bool dl_start(const char *rom_path, const dl_callbacks *callbacks, int link_port);
void dl_stop(void);
bool dl_is_running(void);

// Runs one GBA frame. When `bgra` is non-NULL the frame is drawn into it (240x160, 4 bytes per pixel, B G R A in
// memory order, i.e. Delta's .bgra8); when it is NULL gpSP skips drawing altogether (cheaper).
// Returns true when the game finished writing its save chip about a second ago (time to write the .sav).
bool dl_run_frame(uint8_t *bgra);

// Buttons held, as GBA KEYINPUT bits (A=1 B=2 Select=4 Start=8 Right=16 Left=32 Up=64 Down=128 R=256 L=512),
// which is exactly GBADeltaCore's GBAGameInput raw values.
void dl_set_keys(uint32_t keys);

// Battery saves: the raw chip dump, byte for byte what VBA-M wrote (FireRed: 128 KB flash).
bool dl_load_battery(const char *path);
bool dl_save_battery(const char *path);
size_t dl_battery_size(void);   // bytes dl_save_battery writes for this game (0 = no save chip seen yet)

// Save states (gpSP's own format; VBA-M states from the App Store Delta do not load).
bool dl_save_state(const char *path);
bool dl_load_state(const char *path);

// Cheats: unencrypted Code Breaker codes ("XXXXXXXX YYYY", one or more lines). Returns false if gpSP rejects it.
void dl_cheats_reset(void);
bool dl_cheat_add(const char *code);

// Delta's address space for readMemory: 0x0000-0x7FFF = IWRAM, 0x8000-0x47FFF = EWRAM. NULL when out of range.
const uint8_t *dl_memory(long address, long size);

double dl_frames_per_second(void);

// Link
int dl_link_state(void);
const char *dl_link_error(void);
void dl_link_restart(void);   // re-open the listener (after the app was suspended, the old socket is dead)

#ifdef __cplusplus
}
#endif

#endif
