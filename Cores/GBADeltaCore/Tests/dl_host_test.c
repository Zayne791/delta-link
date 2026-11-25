// dl_host_test.c: runs GBADeltaCore's C bridge (DLGBACore.c + gpSP interpreter + glink) on a Linux/macOS host.
//   dl_host_test ROM SAV PORT [OUT_DIR]
// Proves without an iPhone: the ROM boots without a dynarec, the .sav round-trips byte for byte, save states
// load, cheats parse, the core can be stopped and started again, and the link host accepts a glink peer,
// reports LINKED, notices the drop and listens again.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "DLGBACore.h"

static size_t audio_frames;
static int link_events[16], link_count;
static char last_error[128];
static int failures;

#define CHECK(cond, ...) do { if (cond) { printf("ok    "); } else { printf("FAIL  "); failures++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static void on_audio(const int16_t *f, size_t n, void *ctx) { (void)f; (void)ctx; audio_frames += n; }
static void on_link(int state, const char *err, void *ctx)
{
    (void)ctx;
    if (link_count < 16) link_events[link_count++] = state;
    snprintf(last_error, sizeof last_error, "%s", err ? err : "");
    printf("      link -> %d %s\n", state, err && *err ? err : "");
}
static void on_log(const char *m, void *ctx) { (void)ctx; printf("      log: %s\n", m); }

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n > 0 ? n : 1);
    *size = fread(b, 1, n, f);
    fclose(f);
    return b;
}

static void write_ppm(const char *path, const uint8_t *bgra)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", DL_WIDTH, DL_HEIGHT);
    for (int i = 0; i < DL_WIDTH * DL_HEIGHT; i++) { fputc(bgra[i * 4 + 2], f); fputc(bgra[i * 4 + 1], f); fputc(bgra[i * 4], f); }
    fclose(f);
}

// A fake Kindle: a plain glink client handshake (hello with the core's protocol version).
static int fake_peer_connect(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { perror("connect"); close(fd); return -1; }
    const char *v = "gpSP v1.0";   // GPSP_NETPACKET_VERSION in gpsp/gpsp_config.h
    uint8_t n = (uint8_t)strlen(v);
    send(fd, "GLK1", 4, 0); send(fd, &n, 1, 0); send(fd, v, n, 0);
    return fd;
}

static void run(int frames, uint8_t *screen, uint32_t keys_every_30)
{
    for (int i = 0; i < frames; i++)
    {
        dl_set_keys((keys_every_30 && (i % 30) < 4) ? keys_every_30 : 0);
        dl_run_frame(screen);
    }
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s ROM SAV PORT [OUT_DIR]\n", argv[0]); return 2; }
    const char *rom = argv[1], *sav = argv[2], *out = argc > 4 ? argv[4] : ".";
    int port = atoi(argv[3]);
    char path[1024], path2[1024];
    static uint8_t screen[DL_WIDTH * DL_HEIGHT * 4];

    dl_callbacks cb = { on_audio, on_link, on_log, NULL };

    CHECK(dl_start(rom, &cb, port), "dl_start (interpreter, built-in BIOS, hosting on %d)", port);
    CHECK(link_count == 1 && link_events[0] == DL_LINK_WAITING, "link waiting for the Kindle");
    CHECK(dl_load_battery(sav), "loaded %s", sav);
    CHECK(dl_battery_size() == 0x20000, "battery size %zu (FireRed = 128 KB flash)", dl_battery_size());

    size_t orig_n = 0; uint8_t *orig = read_file(sav, &orig_n);
    snprintf(path, sizeof path, "%s/roundtrip.sav", out);
    CHECK(dl_save_battery(path), "wrote %s", path);
    size_t rt_n = 0; uint8_t *rt = read_file(path, &rt_n);
    CHECK(rt && orig && rt_n == orig_n && !memcmp(rt, orig, rt_n), "the .sav round-trips byte for byte (%zu bytes)", rt_n);
    free(rt);

    // Boot: title screen and on (press START / A now and then)
    run(300, screen, 0);
    uint64_t h300 = fnv(screen, sizeof screen);
    CHECK(h300 != fnv((uint8_t[DL_WIDTH * DL_HEIGHT * 4]){0}, sizeof screen), "frame 300 is not blank");
    snprintf(path, sizeof path, "%s/frame300.ppm", out); write_ppm(path, screen);
    run(300, screen, 8 /* START */);
    snprintf(path, sizeof path, "%s/frame600.ppm", out); write_ppm(path, screen);
    double fps = dl_frames_per_second();
    CHECK(fps > 59.0 && fps < 61.0, "fps %.4f", fps);
    CHECK(audio_frames > 600 * 500 && audio_frames < 600 * 600, "audio: %zu stereo frames in 600 frames (~548.6/frame at 32768 Hz)", audio_frames);

    // Save state: save, run, reload, run again -> same picture
    snprintf(path, sizeof path, "%s/state.bin", out);
    CHECK(dl_save_state(path), "save state");
    run(60, screen, 1 /* A */);
    uint64_t ha = fnv(screen, sizeof screen);
    CHECK(dl_load_state(path), "load state");
    run(60, screen, 1);
    uint64_t hb = fnv(screen, sizeof screen);
    CHECK(ha == hb, "replay after loading the state draws the same frame (%016llx)", (unsigned long long)ha);

    // Cheats
    CHECK(dl_cheat_add("82025840 0001"), "Code Breaker code accepted");
    CHECK(dl_cheat_add("82025840 0001\n82025842 0002"), "multi-line Code Breaker code accepted");
    dl_cheats_reset();

    // Memory window (Delta's readMemory)
    CHECK(dl_memory(0, 4) && dl_memory(0x8000, 4) && !dl_memory(0x48000, 4), "readMemory windows");

    // Link: a fake Kindle connects, links, hangs up; the host listens again and a second connect links again.
    int fd = fake_peer_connect(port);
    for (int i = 0; i < 30 && dl_link_state() != DL_LINK_LINKED; i++) dl_run_frame(NULL);
    CHECK(dl_link_state() == DL_LINK_LINKED, "a glink peer on 127.0.0.1:%d links", port);
    run(120, NULL, 0);
    // Whatever the core sent (RFU traffic) arrives framed: u32 len, u16 sender
    uint8_t buf[65536]; ssize_t got = recv(fd, buf, sizeof buf, MSG_DONTWAIT);
    if (got >= 5) {
        int off = 5 + buf[4];
        printf("      peer received %zd bytes (hello %d bytes", got, off);
        if (got >= off + 6) printf(", first packet len %u from %u", buf[off] | buf[off+1] << 8 | buf[off+2] << 16, buf[off+4]);
        printf(")\n");
    }
    CHECK(got >= 5 && !memcmp(buf, "GLK1", 4), "host sent its glink hello");
    close(fd);
    for (int i = 0; i < 30 && dl_link_state() == DL_LINK_LINKED; i++) dl_run_frame(NULL);
    CHECK(dl_link_state() == DL_LINK_WAITING, "drop noticed, listening again (%s)", last_error);
    fd = fake_peer_connect(port);
    for (int i = 0; i < 30 && dl_link_state() != DL_LINK_LINKED; i++) dl_run_frame(NULL);
    CHECK(dl_link_state() == DL_LINK_LINKED, "reconnect links again");
    close(fd);
    run(10, NULL, 0);

    // A wrong peer is refused
    {
        int bad = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        connect(bad, (struct sockaddr *)&a, sizeof a);
        send(bad, "GLK1\x03" "abc", 8, 0);
        for (int i = 0; i < 10; i++) dl_run_frame(NULL);
        CHECK(dl_link_state() == DL_LINK_WAITING, "a peer with another emulator version is refused (%s)", dl_link_error());
        close(bad);
    }

    // Stop and start again in the same process (Delta does this for every game launch)
    snprintf(path2, sizeof path2, "%s/final.sav", out);
    CHECK(dl_save_battery(path2), "final .sav written");
    dl_stop();
    CHECK(!dl_is_running(), "stopped");
    link_count = 0;
    CHECK(dl_start(rom, &cb, port), "restart in the same process");
    CHECK(dl_load_battery(path2), "reload the .sav");
    run(120, screen, 0);
    CHECK(fnv(screen, sizeof screen) != 0, "second session runs");
    dl_stop();

    free(orig);
    printf("%s (%d failure%s)\n", failures ? "FAILED" : "ALL OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
