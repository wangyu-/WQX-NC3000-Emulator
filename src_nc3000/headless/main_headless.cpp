/*
=====================================================================
 Headless harness for the NC3000 port
=====================================================================
 Runs the emulator core without SDL video/audio so that boot progress
 can be inspected from a script:

   nc3000_headless.exe <romPath> [--ms N] [--trace-ms N] [--dump-lcd f.bmp]
                       [--raw-rom-order]

 It prints a PC histogram (sampled once per emulated millisecond) and
 can dump the 160x80 LCD as a 24 bit BMP, which is how we verify the
 display path without looking at a window.
=====================================================================
*/

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>

#include "comm.h"
#include "cpu.h"
#include "mem.h"
#include "nor.h"
#include "nc2000.h"
#include "NekoDriverIO.h"
#include "ram.h"
#include "settings.h"
#include "spce061_bridge.h"
#include "sound.h"
#include "iv_uart.h"

extern int nc3_dsp_verbose;

/* 墙上时间起点：用来在结尾报"模拟 20 s 花了多少墙上时间"（= 纯模拟速度） */
static std::chrono::steady_clock::time_point g_headless_t0;

static uint8_t lcd_shot[SCREEN_WIDTH * SCREEN_HEIGHT * 2];

/* stubs so the GUI-less build can link cmd.cpp / console.cpp */
struct SDL_Window;
SDL_Window *window = nullptr;
uint8_t lcd_buf[SCREEN_WIDTH * SCREEN_HEIGHT / 8 * 2];

/* ---------------------------------------------------------------- tracing */
static uint32_t pc_hist[0x10000];
static uint8_t  page_seen[0x100];
static uint64_t brk_count = 0;
static uint32_t int_hist[0x10000 / 1];   /* keyed by (bank<<8)|idx */
struct BrkRec { uint16_t pc, idx, bank; };
static std::vector<BrkRec> brks;
static uint64_t insn_count = 0;
static bool tracing = false;

/* ------------------------------------------------------------------
 * ring buffer of the last instructions executed, dumped the first time
 * PC hits a caller-supplied address.  This is how we find out who calls
 * the "power off" routine at $E36D.
 * ------------------------------------------------------------------ */
static const int RING_N = 1 << 16;
static uint16_t ring[RING_N];
static uint32_t ring_pos = 0;
static bool ring_active = false;
static bool ring_done = false;
static uint16_t ring_trigger = 0;
static uint32_t ring_seen = 0;
static std::string ring_out_path;
static std::string ring_mem_path;

/* watch points: print "who jumped here" plus the 6502 call stack */
static const int WATCH_MAX = 8;
static uint16_t watch_tbl[WATCH_MAX];
static int watch_n = 0;
static uint16_t watch_prev = 0;
static bool watch_prev_valid = false;
static uint32_t watch_hits = 0;
static int watch_max_hits = 8;

/* --watch-last <pc>: remember the *last* transfer into <pc> and print it at
 * the end of the run (useful when the interesting event is the final one). */
static const int WATCHLAST_MAX = 8;
static uint16_t wlast_tbl[WATCHLAST_MAX];
static int wlast_n = 0;
static uint16_t wlast_from[WATCHLAST_MAX];
static uint64_t wlast_cycles[WATCHLAST_MAX];
static uint8_t wlast_regs[WATCHLAST_MAX][5];
static uint16_t wlast_prev = 0;
static bool wlast_prev_valid = false;

/* --watch-write <addr>: which PC stores into that RAM/ROM address */
static const int WW_MAX = 4;
static uint16_t ww_tbl[WW_MAX];
static uint8_t ww_last[WW_MAX];
static int ww_n = 0;
static uint16_t ww_prev_pc = 0;
static bool ww_prev_valid = false;

/* --stack-at <pc>: dump the 6502 stack (and the bank regs) every time <pc>
 * executes, for the first few occurrences.  Used to inspect the far-call /
 * far-return trampoline at $E9C7. */
static const int STACKAT_MAX = 4;
static uint16_t stackat_tbl[STACKAT_MAX];
static int stackat_n = 0;
static int stackat_hits = 0;
static int stackat_limit = 3;
static uint16_t stackat_prev = 0;
static bool stackat_prev_valid = false;

/* --count <pc>: how many times an address is executed (cheap "hot spot" probe) */
static const int COUNT_MAX = 8;
static uint16_t count_tbl[COUNT_MAX];
static uint64_t count_val[COUNT_MAX];
static int count_n = 0;

static void wlast_report() {
    for (int k = 0; k < wlast_n; k++) {
        if (wlast_cycles[k] == 0) { printf("watch-last $%04X: never hit\n", wlast_tbl[k]); continue; }
        printf("watch-last $%04X: last entered from $%04X at cycles=%llu "
               "(A=%02X X=%02X Y=%02X SP=%02X P=%02X)\n",
               wlast_tbl[k], wlast_from[k], (unsigned long long)wlast_cycles[k],
               wlast_regs[k][0], wlast_regs[k][1], wlast_regs[k][2],
               wlast_regs[k][3], wlast_regs[k][4]);
    }
}

static void watch_report(uint16_t pc) {
    printf("### hit $%04X  from $%04X  (hit #%u)  cycles=%llu\n",
           pc, watch_prev, ++watch_hits,
           (unsigned long long)nc2k_states.cycles);
    printf("    A=%02X X=%02X Y=%02X SP=%02X P=%02X  bs=%02X roa=%02X clk=%02X\n",
           cpu->A, cpu->X, cpu->Y, cpu->SP, cpu->P(),
           nc2k_states.ram_io[0x00], nc2k_states.ram_io[0x0a],
           nc2k_states.ram_io[0x05]);
    printf("    stack (top = lowest address above SP=$%02X):\n", cpu->SP);
    for (int row = 0; row < 16; row++) {
        printf("      $01%X0:", row);
        for (int c = 0; c < 16; c++)
            printf(" %02X", Peek16((uint16_t)(0x0100 + row * 16 + c)));
        printf("\n");
    }
    printf("    ram_io:");
    for (int a = 0; a < 0x20; a++) {
        printf(" %02X", nc2k_states.ram_io[a]);
        if (a % 16 == 15) printf("\n           ");
    }
    printf("\n");
    fflush(stdout);
}

static void ring_dump() {
    printf("=== ring dump: last %u instructions before PC hit $%04X ===\n",
           RING_N, ring_trigger);
    uint32_t n = ring_seen < (uint32_t)RING_N ? ring_seen : (uint32_t)RING_N;
    uint32_t start = (ring_pos + RING_N - n) % RING_N;
    uint32_t i = 0;
    while (i < n) {
        uint16_t pc = ring[(start + i) % RING_N];
        uint32_t j = i + 1;
        while (j < n) {
            uint16_t prev = ring[(start + j - 1) % RING_N];
            uint16_t cur = ring[(start + j) % RING_N];
            /* a 6502 instruction is at most 3 bytes: treat "pc+1..pc+3" as the
             * same run so straight-line code collapses nicely */
            if (cur == (uint16_t)(prev + 1) || cur == (uint16_t)(prev + 2) ||
                cur == (uint16_t)(prev + 3))
                j++;
            else
                break;
        }
        if (j - i <= 6) {
            for (uint32_t k = i; k < j; k++)
                printf("%04X%s", ring[(start + k) % RING_N],
                       (k + 1 < j) ? " " : " ");
        } else {
            printf("[$%04X .. $%04X] x%u", pc,
                   ring[(start + j - 1) % RING_N], j - i);
        }
        printf("\n");
        i = j;
    }
    printf("=== registers: PC=%04X A=%02X X=%02X Y=%02X SP=%02X P=%02X\n",
           cpu->PC, cpu->A, cpu->X, cpu->Y, cpu->SP, cpu->P());
    printf("    ram_io: bs=%02X roa=%02X clk=%02X ctrl=%02X ramb=%02X zp=%02X\n",
           nc2k_states.ram_io[0x00], nc2k_states.ram_io[0x0a],
           nc2k_states.ram_io[0x05], nc2k_states.ram_io[0x0b],
           nc2k_states.ram_io[0x0d], nc2k_states.ram_io[0x0f]);
    printf("    window mapping:");
    for (int b = 0; b < 8; b++) {
        long off = -1;
        if (memmap[b] >= nor_buff && memmap[b] < nor_buff + sizeof(nor_buff))
            off = (long)(memmap[b] - nor_buff);
        printf(" m%d=%s%s", b,
               off >= 0 ? "nor+" : "",
               off >= 0 ? std::to_string(off).c_str() : "ram");
    }
    printf("\n");
    printf("    stack $0100-$01FF:\n");
    for (int r = 0; r < 16; r++) {
        printf("      $01%X0:", r);
        for (int c = 0; c < 16; c++) printf(" %02X", Peek16((uint16_t)(0x0100 + r * 16 + c)));
        printf("\n");
    }
    printf("=== end ring dump ===\n");
    if (!ring_mem_path.empty()) {
        FILE *f = fopen(ring_mem_path.c_str(), "wb");
        if (f) {
            for (int b = 0; b < 8; b++) fwrite(memmap[b], 1, 0x2000, f);
            fclose(f);
            printf("ring mem image ($0000-$FFFF as seen by the cpu) -> %s\n",
                   ring_mem_path.c_str());
        } else {
            printf("cannot write ring mem image %s\n", ring_mem_path.c_str());
        }
    }
    if (!ring_out_path.empty()) {
        FILE *f = fopen(ring_out_path.c_str(), "w");
        if (f) {
            uint32_t rn = ring_seen < (uint32_t)RING_N ? ring_seen : (uint32_t)RING_N;
            uint32_t rstart = (ring_pos + RING_N - rn) % RING_N;
            for (uint32_t k = 0; k < rn;) {
                uint16_t pc = ring[(rstart + k) % RING_N];
                uint32_t m = k + 1;
                while (m < rn && ring[(rstart + m) % RING_N] == pc) m++;
                fprintf(f, "%04X x%u\n", pc, m - k);
                k = m;
            }
            fclose(f);
            printf("ring dump written to %s\n", ring_out_path.c_str());
        } else {
            printf("cannot write ring dump %s\n", ring_out_path.c_str());
        }
    }
    fflush(stdout);
}

static void trace_cb(uint16_t pc) {
    if (!tracing) return;
    insn_count++;
    /* --watch-write <addr>: report the PC that stored into <addr>.
     * The per-instruction callback tells us the *next* PC, so a change we
     * notice here was caused by the instruction that just ran (ww_prev_pc). */
    if (ww_n) {
        for (int k = 0; k < ww_n; k++) {
            uint8_t v = Peek16(ww_tbl[k]);
            if (ww_prev_valid && v != ww_last[k]) {
                printf("### write $%04X = %02X  by PC=$%04X  (bank bs=%02X roa=%02X "
                       "A=%02X X=%02X Y=%02X) cycles=%llu\n",
                       ww_tbl[k], v, ww_prev_pc,
                       nc2k_states.ram_io[0x00], nc2k_states.ram_io[0x0a],
                       cpu->A, cpu->X, cpu->Y,
                       (unsigned long long)nc2k_states.cycles);
            }
            ww_last[k] = v;
        }
        ww_prev_pc = pc;
        ww_prev_valid = true;
    }
    if (watch_n) {
        for (int k = 0; k < watch_n; k++) {
            if (pc == watch_tbl[k] && watch_prev_valid &&
                watch_hits < (uint32_t)watch_max_hits)
                watch_report(pc);
        }
        watch_prev = pc;
        watch_prev_valid = true;
    }
    if (wlast_n) {
        for (int k = 0; k < wlast_n; k++) {
            if (pc == wlast_tbl[k] && wlast_prev_valid) {
                wlast_from[k] = wlast_prev;
                wlast_cycles[k] = nc2k_states.cycles;
                wlast_regs[k][0] = (uint8_t)cpu->A;
                wlast_regs[k][1] = (uint8_t)cpu->X;
                wlast_regs[k][2] = (uint8_t)cpu->Y;
                wlast_regs[k][3] = (uint8_t)cpu->SP;
                wlast_regs[k][4] = (uint8_t)cpu->P();
            }
        }
        wlast_prev = pc;
        wlast_prev_valid = true;
    }
    if (count_n) {
        for (int k = 0; k < count_n; k++)
            if (pc == count_tbl[k]) count_val[k]++;
    }
    if (stackat_n) {
        for (int k = 0; k < stackat_n; k++) {
            if (pc == stackat_tbl[k] && stackat_hits < stackat_limit) {
                stackat_hits++;
                printf("### stack@$%04X (hit %d) SP=%02X bank=%02X roa=%02X "
                       "prev=%04X cycles=%llu\n",
                       pc, stackat_hits, cpu->SP, nc2k_states.ram_io[0x00],
                       nc2k_states.ram_io[0x0a], stackat_prev,
                       (unsigned long long)nc2k_states.cycles);
                printf("    $0100..$01FF:");
                for (int a = 0; a < 0x100; a++) {
                    if (a % 16 == 0) printf("\n      $01%X0:", a / 16);
                    printf(" %02X", Peek16((uint16_t)(0x0100 + a)));
                }
                printf("\n");
                fflush(stdout);
            }
        }
        stackat_prev = pc;
        stackat_prev_valid = true;
    }
    if (ring_active && !ring_done) {
        ring[ring_pos] = pc;
        ring_pos = (ring_pos + 1) % RING_N;
        ring_seen++;
        if (pc == ring_trigger) {
            ring_done = true;
            ring_dump();
        }
    }
    pc_hist[pc]++;
    page_seen[pc >> 8] = 1;
    if (Peek16(pc) == 0x00) {                 /* BRK / INT <idx> <bank> */
        brk_count++;
        int_hist[((uint32_t)Peek16((uint16_t)(pc + 2)) << 8) |
                 (uint32_t)Peek16((uint16_t)(pc + 1))]++;
        if (brks.size() < 40)
            brks.push_back({pc, (uint16_t)Peek16((uint16_t)(pc + 1)),
                                (uint16_t)Peek16((uint16_t)(pc + 2))});
    }
}

/* who touches the UART, and where from */
static uint32_t pc_at_3a_w[0x10000];
static uint32_t pc_at_3c_r[0x10000];
static uint32_t pc_at_0e_r[0x10000];
static uint32_t pc_at_08_r[0x10000];
static uint32_t pc_at_1e_r[0x10000];
static uint32_t cnt_3a_w_bk0 = 0, cnt_3a_w_other = 0;
static uint8_t snap_2000[0x2000];
static bool snap_2000_taken = false;

/* --io-watch <port>: trace writes to one IO address (with the writing PC).
 * --io-after <ms> / --io-max <n> bound the trace so it can be aimed at the
 * interesting part of the run (the log is otherwise gigabytes). */
static const int IOW_MAX = 4;
static int  iow_tbl[IOW_MAX];
static int  iow_n = 0;
static int  ior_tbl[IOW_MAX];      /* --io-rwatch <port>: trace *reads* */
static int  ior_n = 0;
static int  ior_hits = 0;
static int  iow_after_ms = 0;
static int  iow_max = 40;
static int  iow_hits = 0;
static int  nand_after_ms = 0;   /* CYCLES_SECOND is not valid until Init() */

/* --dump-061 <file.wav>: capture the SPCE061A's raw DAC stream *before* the
 * mixer (no beeper, no resampling) - the only honest way to tell "the chip is
 * noisy" from "the mixer/beep is noisy". */
static std::string dump061_path;
static FILE *dump061_fp = NULL;
static uint32_t dump061_samples = 0;
static uint32_t dump061_rate = 36790;

static void dump061_begin() {
    dump061_fp = fopen(dump061_path.c_str(), "wb");
    if (!dump061_fp) { printf("cannot write %s\n", dump061_path.c_str()); return; }
    uint8_t hdr[44] = {0};
    uint32_t v; uint16_t w;
    memcpy(hdr, "RIFF", 4); memcpy(hdr + 8, "WAVEfmt ", 8);
    v = 16; memcpy(hdr + 16, &v, 4);
    w = 1;  memcpy(hdr + 20, &w, 2);
    w = 1;  memcpy(hdr + 22, &w, 2);
    v = dump061_rate; memcpy(hdr + 24, &v, 4);
    v = dump061_rate * 2; memcpy(hdr + 28, &v, 4);
    w = 2;  memcpy(hdr + 32, &w, 2);
    w = 16; memcpy(hdr + 34, &w, 2);
    memcpy(hdr + 36, "data", 4);
    fwrite(hdr, 1, 44, dump061_fp);
}

static void dump061_pump() {
    int16_t buf[512];
    int got;
    if (!dump061_fp) return;
    /* Label the file with whatever the highest rate seen was (S600 words play at
     * 36,790 Hz; the idle DAC rate is ~8 kHz and only emits DC). */
    {
        uint32_t r = nc3_dsp_sample_rate();
        if (r > dump061_rate) dump061_rate = r;
    }
    for (;;) {
        got = nc3_dsp_audio(buf, 512);
        if (got <= 0) break;
        fwrite(buf, sizeof(int16_t), (size_t)got, dump061_fp);
        dump061_samples += (uint32_t)got;
        if (got < 512) break;
    }
}

static void dump061_end() {
    if (!dump061_fp) return;
    uint32_t v;
    v = dump061_rate;             fseek(dump061_fp, 24, SEEK_SET); fwrite(&v, 4, 1, dump061_fp);
    v = dump061_rate * 2;         fseek(dump061_fp, 28, SEEK_SET); fwrite(&v, 4, 1, dump061_fp);
    v = 36 + dump061_samples * 2; fseek(dump061_fp, 4, SEEK_SET);  fwrite(&v, 4, 1, dump061_fp);
    v = dump061_samples * 2;      fseek(dump061_fp, 40, SEEK_SET); fwrite(&v, 4, 1, dump061_fp);
    fclose(dump061_fp);
    dump061_fp = NULL;
    printf("[061-dump] wrote %u samples @ %u Hz\n", dump061_samples, dump061_rate);
}

static void io_cb(int is_write, int addr, int value) {
    if (!is_write && ior_n && ior_hits < iow_max) {
        for (int k = 0; k < ior_n; k++) {
            if (addr != ior_tbl[k]) continue;
            if ((int)(nc2k_states.cycles / (CYCLES_SECOND / 1000)) < iow_after_ms) break;
            ior_hits++;
            printf("[ior] t=%ums pc=$%04X  $%02X -> %02X  (A=%02X X=%02X Y=%02X)\n",
                   (unsigned)(nc2k_states.cycles / (CYCLES_SECOND / 1000)),
                   cpu ? (uint16_t)cpu->PC : 0, addr, value,
                   cpu ? (uint8_t)cpu->A : 0, cpu ? (uint8_t)cpu->X : 0,
                   cpu ? (uint8_t)cpu->Y : 0);
            break;
        }
    }
    if (is_write && iow_n && iow_hits < iow_max) {
        int k;
        for (k = 0; k < iow_n; k++) {
            if (addr != iow_tbl[k]) continue;
            if ((int)(nc2k_states.cycles / (CYCLES_SECOND / 1000)) < iow_after_ms) break;
            iow_hits++;
            printf("[io] t=%ums  pc=$%04X  $%02X <- %02X  (A=%02X X=%02X Y=%02X)\n",
                   (unsigned)(nc2k_states.cycles / (CYCLES_SECOND / 1000)),
                   cpu ? (uint16_t)cpu->PC : 0, addr, value,
                   cpu ? (uint8_t)cpu->A : 0, cpu ? (uint8_t)cpu->X : 0,
                   cpu ? (uint8_t)cpu->Y : 0);
            break;
        }
    }
    if (is_write && addr == 0x04) {
        static int n04 = 0;
        if (n04 < 60) { n04++; printf("[io04] w=%02X pc=%04X\n", value, cpu->PC); }
    }
    if (is_write && addr == 0x3a) pc_at_3a_w[cpu->PC]++;
    else if (!is_write && addr == 0x08) pc_at_08_r[cpu->PC]++;
    else if (!is_write && addr == 0x1e) pc_at_1e_r[cpu->PC]++;
    else if (!is_write && addr == 0x3c) {
        pc_at_3c_r[cpu->PC]++;
        if (!snap_2000_taken && cpu->PC >= 0x2000 && cpu->PC < 0x4000) {
            memcpy(snap_2000, memmap[1], sizeof(snap_2000));
            snap_2000_taken = true;
        }
    }
    else if (!is_write && addr == 0x0e) pc_at_0e_r[cpu->PC]++;
}

static void print_top(const char *label, uint32_t *hist, int n, uint64_t total) {
    printf("%s (top %d):\n", label, n);
    for (int k = 0; k < n; k++) {
        int best = -1; uint32_t bestv = 0;
        for (int i = 0; i < 0x10000; i++) if (hist[i] > bestv) { bestv = hist[i]; best = i; }
        if (best < 0 || bestv == 0) break;
        printf("   %04X : %u (%.1f%%)\n", best, bestv,
               total ? 100.0 * bestv / total : 0.0);
        hist[best] = 0;
    }
}

static void put_u32(FILE *f, uint32_t v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}
static void put_u16(FILE *f, uint16_t v) {
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}

/* 4 gray levels, same palette as display.cpp */
static const uint8_t gray4[4] = {255, 180, 105, 0};

static bool dump_lcd_bmp(const char *path, bool grey) {
    if (!CopyLcdBuffer(lcd_shot)) return false;

    const int w = SCREEN_WIDTH, h = SCREEN_HEIGHT;
    const int rowbytes = (w * 3 + 3) & ~3;
    const uint32_t imgsize = rowbytes * h;

    FILE *f = fopen(path, "wb");
    if (!f) { printf("cannot write %s\n", path); return false; }

    fputc('B', f); fputc('M', f);
    put_u32(f, 54 + imgsize);
    put_u16(f, 0); put_u16(f, 0);
    put_u32(f, 54);

    put_u32(f, 40); put_u32(f, w); put_u32(f, h);
    put_u16(f, 1); put_u16(f, 24); put_u32(f, 0);
    put_u32(f, imgsize); put_u32(f, 2835); put_u32(f, 2835);
    put_u32(f, 0); put_u32(f, 0);

    std::vector<uint8_t> row(rowbytes, 0);
    for (int y = h - 1; y >= 0; y--) {          /* BMP is bottom-up */
        for (int x = 0; x < w; x++) {
            int v;
            if (grey) {
                int i = (y * w + x) / 4;
                int j = (y * w + x) % 4;
                v = (lcd_shot[i] >> (6 - j * 2)) & 0x03;
            } else {
                int i = (y * w + x) / 8;
                int j = (y * w + x) % 8;
                v = (lcd_shot[i] & (1 << (7 - j))) ? 3 : 0;
            }
            row[x * 3 + 0] = gray4[v];
            row[x * 3 + 1] = gray4[v];
            row[x * 3 + 2] = gray4[v];
        }
        fwrite(&row[0], 1, rowbytes, f);
    }
    fclose(f);
    return true;
}

int main(int argc, char *argv[]) {
    std::string rom = argc > 1 ? argv[1] : "roms/nc3000";
    int ms = 3000;
    int trace_ms = 0;
    std::string dump = "out/lcd.bmp";
    bool raw_order = false;
    std::vector<std::string> extra;
    uint32_t dump_ram_start = 0, dump_ram_len = 0;
    std::string dump_ram_path;
    std::string dump_all_ram_path;
    struct KeyEv { int ms, y, x; };
    std::vector<KeyEv> keys;
    int key_hold = 150;
    std::vector<std::pair<int, std::string> > snaps;
    int seq_ms = 0;
    std::string seq_prefix;
    int play_at_ms = -1;
    std::string play_file;
    std::vector<uint8_t> play_data;
    int probe_ms = -1;
    int dump_nand_page = -1;
    uint32_t ring_target = 0;
    struct CmdEv { int ms; std::string cmd; };
    std::vector<CmdEv> cmd_events;          /* --cmd-at <ms> "<emulator cmd>" */
    std::string save_flash_prefix;          /* --save-flash <prefix> */

    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--ms") && i + 1 < argc) ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--trace-ms") && i + 1 < argc) trace_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-lcd") && i + 1 < argc) dump = argv[++i];
        else if (!strcmp(argv[i], "--raw-rom-order")) raw_order = true;
        else if (!strcmp(argv[i], "--dsp-trace")) nc3_dsp_verbose = 1;
        else if (!strcmp(argv[i], "--uart-log") && i + 1 < argc)
            uart_log_level = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-all-ram") && i + 1 < argc) dump_all_ram_path = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 3 < argc) {
            keys.push_back({atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3])});
            i += 3;
        }
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc) key_hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--count") && i + 1 < argc) {
            if (count_n < COUNT_MAX)
                count_tbl[count_n++] = (uint16_t)strtoul(argv[++i], nullptr, 16);
            else
                ++i;
        }
        else if (!strcmp(argv[i], "--stack-at") && i + 1 < argc) {
            if (stackat_n < STACKAT_MAX)
                stackat_tbl[stackat_n++] = (uint16_t)strtoul(argv[++i], nullptr, 16);
            else
                ++i;
        }
        else if (!strcmp(argv[i], "--stack-hits") && i + 1 < argc)
            stackat_limit = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lcd-seq") && i + 2 < argc) {
            seq_ms = atoi(argv[i + 1]);
            seq_prefix = argv[i + 2];
            i += 2;
        }
        else if (!strcmp(argv[i], "--dsp-play") && i + 2 < argc) {
            play_at_ms = atoi(argv[i + 1]);
            play_file = argv[i + 2];
            i += 2;
        }
        else if (!strcmp(argv[i], "--key-probe") && i + 1 < argc) {
            probe_ms = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--dump-nand-page") && i + 1 < argc) {
            dump_nand_page = strtoul(argv[++i], nullptr, 0);
        }
        else if (!strcmp(argv[i], "--ring") && i + 1 < argc) {
            ring_target = strtoul(argv[++i], nullptr, 16);
        }
        else if (!strcmp(argv[i], "--ring-out") && i + 1 < argc) {
            ring_out_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--ring-mem") && i + 1 < argc) {
            ring_mem_path = argv[++i];
        }
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) {
            if (watch_n < WATCH_MAX)
                watch_tbl[watch_n++] = (uint16_t)strtoul(argv[++i], nullptr, 16);
            else
                ++i;
        }
        else if (!strcmp(argv[i], "--watch-last") && i + 1 < argc) {
            if (wlast_n < WATCHLAST_MAX)
                wlast_tbl[wlast_n++] = (uint16_t)strtoul(argv[++i], nullptr, 16);
            else
                ++i;
        }
        else if (!strcmp(argv[i], "--watch-write") && i + 1 < argc) {
            if (ww_n < WW_MAX)
                ww_tbl[ww_n++] = (uint16_t)strtoul(argv[++i], nullptr, 16);
            else
                ++i;
        }
        else if (!strcmp(argv[i], "--io-watch") && i + 1 < argc) {
            if (iow_n < IOW_MAX) iow_tbl[iow_n++] = (int)strtol(argv[++i], nullptr, 0);
            else ++i;
        }
        else if (!strcmp(argv[i], "--io-rwatch") && i + 1 < argc) {
            if (ior_n < IOW_MAX) ior_tbl[ior_n++] = (int)strtol(argv[++i], nullptr, 0);
            else ++i;
        }
        else if (!strcmp(argv[i], "--watch-read") && i + 1 < argc) {
            /* --watch-read <addr> [len]：谁读了这段内存（找字符串/表的引用最快） */
            extern int dbg_load_watch_addr, dbg_load_watch_len;
            dbg_load_watch_addr = (int)strtol(argv[++i], nullptr, 0);
            dbg_load_watch_len = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') dbg_load_watch_len = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--io-after") && i + 1 < argc) iow_after_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--io-max") && i + 1 < argc) iow_max = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nand-after") && i + 1 < argc)
            nand_after_ms = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--nand-watch-sector") && i + 1 < argc)
            nand_watch_page = (uint32_t)strtoul(argv[++i], nullptr, 0) * 32;
        else if (!strcmp(argv[i], "--dump-061") && i + 1 < argc) dump061_path = argv[++i];
        else if (!strcmp(argv[i], "--snap-at") && i + 2 < argc) {
            snaps.push_back(std::make_pair(atoi(argv[i + 1]), std::string(argv[i + 2])));
            i += 2;
        }
        else if (!strcmp(argv[i], "--dump-ram") && i + 3 < argc) {
            dump_ram_start = strtoul(argv[i + 1], nullptr, 0);
            dump_ram_len   = strtoul(argv[i + 2], nullptr, 0);
            dump_ram_path  = argv[i + 3];
            i += 3;
        }
        else if (!strcmp(argv[i], "--cmd-at") && i + 2 < argc) {
            CmdEv e; e.ms = atoi(argv[i + 1]); e.cmd = argv[i + 2];
            cmd_events.push_back(e);
            i += 2;
        }
        else if (!strcmp(argv[i], "--save-flash") && i + 1 < argc) {
            save_flash_prefix = argv[++i];
        }
        else if (!strcmp(argv[i], "--cmd-file") && i + 2 < argc) {
            /* 和 --cmd-at 一样，但命令字符串按“原始字节”从文件读，便于传 GBK 文件名 */
            int at = atoi(argv[i + 1]);
            FILE *cf = fopen(argv[i + 2], "rb");
            if (!cf) { printf("cannot open cmd file %s\n", argv[i + 2]); }
            else {
                std::string s;
                int ch;
                while ((ch = fgetc(cf)) != EOF) {
                    if (ch == '\r' || ch == '\n') continue;
                    s.push_back((char)ch);
                }
                fclose(cf);
                CmdEv e; e.ms = at; e.cmd = s;
                cmd_events.push_back(e);
            }
            i += 2;
        }
        else extra.push_back(argv[i]);      /* forwarded to the emulator */
    }
    if (raw_order) nor_read_format = NorFormat::PHYSICAL_ORDER;

    std::vector<std::string> a;
    a.push_back(argv[0]);
    a.push_back("--nc3000");
    a.push_back("--rom");
    a.push_back(rom);
    for (auto &e : extra) a.push_back(e);
    std::vector<char *> av;
    for (auto &s : a) av.push_back((char *)s.c_str());
    process_args((int)av.size(), av.data());

    LoadNC2k();
    if (nand_after_ms > 0)
        nand_log_from_cycle = (uint64_t)nand_after_ms * (CYCLES_SECOND / 1000);
    if (dump_nand_page >= 0) {
        extern const uint8_t* nand_device_page_ptr(uint32_t page);
        printf("=== NAND device pages (as the emulator sees them) ===\n");
        for (int p = 0; p <= 3; p++) {
            const uint8_t *d = nand_device_page_ptr((uint32_t)p);
            if (!d) { printf("  page %d: out of range\n", p); continue; }
            printf("  page %-6d: ", p);
            for (int k = 0; k < 16; k++) printf("%02X ", d[k]);
            printf("\n");
        }
        for (int p = 63; p <= 66; p++) {
            const uint8_t *d = nand_device_page_ptr((uint32_t)p);
            if (!d) { printf("  page %d: out of range\n", p); continue; }
            printf("  page %-6d: ", p);
            for (int k = 0; k < 16; k++) printf("%02X ", d[k]);
            printf("\n");
        }
        {
            uint32_t p = 101855, q = 101856;
            const uint8_t *d = nand_device_page_ptr(p);
            if (d) { printf("  page %-6d: ", p); for (int k=0;k<16;k++) printf("%02X ", d[k]); printf("\n"); }
            d = nand_device_page_ptr(q);
            if (d) { printf("  page %-6d: ", q); for (int k=0;k<16;k++) printf("%02X ", d[k]); printf("\n"); }
        }
        printf("=== end ===\n");
    }
    if(!dump_audio_path.empty()) dump_audio_begin();
    if(!dump061_path.empty()) {
        extern int dsp061_drain_disabled;
        dsp061_drain_disabled = 1;      /* the raw tap owns the stream in this run */
        dump061_rate = 0;
        dump061_begin();
    }
    if (play_at_ms >= 0) {
        FILE *f = fopen(play_file.c_str(), "rb");
        if (!f) { printf("cannot read %s\n", play_file.c_str()); return 2; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        play_data.resize((size_t)sz);
        if (sz > 0) { size_t rd = fread(&play_data[0], 1, (size_t)sz, f); (void)rd; }
        fclose(f);
        printf("dsp-play: %ld bytes (%ld frames) loaded\n", sz, sz / 18);
    }
    if (probe_ms >= 0) {
        void SetKeyWayback(int code_y, int code_x, bool down_or_up);
        for (int t = 0; t < probe_ms; t++) RunTimeSlice(1);
        /* wake the unit first: 开关键 = 矩阵 (4,0)（$18 ONOFF_KEY），
         * 在本模拟器里用它把机器从 clk off 状态唤醒 */
        SetKeyWayback(4, 0, true);
        for (int t = 0; t < 200; t++) RunTimeSlice(1);
        SetKeyWayback(4, 0, false);
        for (int t = 0; t < 3000; t++) RunTimeSlice(1);   /* let it boot */
        printf("=== key probe: (col,bit) -> firmware key code ===\n");
        printf("    code = $C7 & 0x7F while pressed ('--' = nothing decoded)\n");
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 16; x++) {
                uint8_t k1, k2, k3;
                if ((y == 0 && x == 0) || (y == 4 && x == 0)) {
                    printf("  (%d,%d)  --      (ON/OFF and reset, skipped: they power the unit off)\n", y, x);
                    continue;
                }
                SetKeyWayback(y, x, true);
                for (int t = 0; t < 90; t++) RunTimeSlice(1);
                k1 = (uint8_t)Peek16(0x00C7);
                k2 = (uint8_t)Peek16(0x040A);
                SetKeyWayback(y, x, false);
                for (int t = 0; t < 110; t++) RunTimeSlice(1);
                k3 = (uint8_t)Peek16(0x00C7);
                printf("  (%d,%d)  %s   $C7=%02X $040A=%02X  rel=%02X\n",
                       y, x, (k1 & 0x80) ? "--" : "  ", k1, k2, k3);
                fflush(stdout);
            }
        }
        printf("=== probe done ===\n");
    }
    printf("after boot: PC=%04X A=%02X X=%02X Y=%02X SP=%02X cycles=%llu\n",
           cpu->PC, cpu->A, cpu->X, cpu->Y, cpu->SP,
           (unsigned long long)nc2k_states.cycles);

    headless_pc_cb = trace_cb;
    headless_io_cb = io_cb;
    nand_blk_counting = true;
    io_access_counting = true;
    tracing = true;
    if (ring_target) {
        ring_trigger = (uint16_t)ring_target;
        ring_active = true;
        ring_done = false;
        ring_pos = 0;
        ring_seen = 0;
        printf("ring: will dump the last %d instructions when PC hits $%04X\n",
               RING_N, ring_trigger);
    }
    g_headless_t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < ms; t++) {
        for (auto &k : keys) {
            void SetKeyWayback(int code_y, int code_x, bool down_or_up);
            if (t == k.ms) {
                printf("[%d ms] press key (%d,%d)\n", t, k.y, k.x);
                SetKeyWayback(k.y, k.x, true);
            }
            if (t == k.ms + key_hold) SetKeyWayback(k.y, k.x, false);
        }
        RunTimeSlice(1);
        for (size_t ci = 0; ci < cmd_events.size(); ci++) {
            if (t == cmd_events[ci].ms) {
                printf("[%d ms] EMULATOR CMD: %s\n", t, cmd_events[ci].cmd.c_str());
                fflush(stdout);
                void handle_cmd(string str);
                handle_cmd(cmd_events[ci].cmd);
                fflush(stdout);
            }
        }
        for (size_t si = 0; si < snaps.size(); si++) {
            if (t == snaps[si].first) {
                FILE *f = fopen(snaps[si].second.c_str(), "wb");
                if (f) {
                    for (int b = 0; b < 8; b++) fwrite(memmap[b], 1, 0x2000, f);
                    fclose(f);
                    printf("[%d ms] mem image -> %s\n", t, snaps[si].second.c_str());
                } else {
                    printf("[%d ms] cannot write %s\n", t, snaps[si].second.c_str());
                }
            }
        }
        if (play_at_ms >= 0 && t == play_at_ms)
            nc3_dsp_play_begin(play_data.empty() ? nullptr : &play_data[0],
                               (uint32_t)play_data.size());
        if (play_at_ms >= 0 && t >= play_at_ms) nc3_dsp_play_tick();
        if(!dump_audio_path.empty()) dump_audio_pump(BEEPER_AUDIO_HZ / 1000);
        if(!dump061_path.empty()) dump061_pump();
        if(!dump061_path.empty() && trace_ms > 0 && t % trace_ms == 0)
            printf("[061-dump] t=%dms rate=%u samples=%u\n", t, nc3_dsp_sample_rate(),
                   dump061_samples);
        if (seq_ms > 0 && t % seq_ms == 0) {
            char path[512];
            snprintf(path, sizeof path, "%s_%05d.bmp", seq_prefix.c_str(), t);
            dump_lcd_bmp(path, is_grey_mode());
        }
        if (trace_ms > 0 && t % trace_ms == 0) {
            int npressed = 0;
            char kbuf[256];
            kbuf[0] = 0;
            for (int ky = 0; ky < 8; ky++)
                for (int kx = 0; kx < 16; kx++)
                    if (keypadmatrix[ky][kx]) {
                        npressed++;
                        snprintf(kbuf + strlen(kbuf), sizeof kbuf - strlen(kbuf),
                                 " (%d,%d)", ky, kx);
                    }
            printf("[%5d ms] cycles=%-10llu PC=%04X SP=%02X A=%02X lcdaddr=%04X "
                   "lcdbuffaddr=%04X masked=%04X mask=%04X io06=%02X io0c=%02X "
                   "io0b=%02X io10=%02X keys=%d%s $C7=%02X lcden=%d lcdon=%d\n",
                   t, (unsigned long long)nc2k_states.cycles, cpu->PC, cpu->SP, cpu->A,
                   nc2k_states.lcdbuffaddr, nc2k_states.lcdbuffaddr,
                   (uint16_t)(nc2k_states.lcdbuffaddr & nc2k_states.lcdbuffaddrmask),
                   nc2k_states.lcdbuffaddrmask,
                   nc2k_states.ram_io[0x06], nc2k_states.ram_io[0x0c],
                   nc2k_states.ram_io[0x0b], nc2k_states.ram_io[0x10],
                   npressed, kbuf, (unsigned)Peek16(0x00C7),
                   (int)nc2k_states.lcden, (int)nc2k_states.lcdon);
            printf("        rtc: s=%02X m=%02X h=%02X ms=%02X  $03FE=%02X $03F9=%02X "
                   "$0406=%02X $0408=%02X $0A90=%02X $0A94=%02X $03ED=%02X $0401=%02X $0405=%02X "
                   "CE=%02X $03E3=%02X $03E4=%02X $03E5=%02X $03F1=%02X $B9=%02X\n",
                   nc2k_states.ext_reg[0], nc2k_states.ext_reg[1],
                   nc2k_states.ext_reg[2], nc2k_states.ext_reg[4],
                   (unsigned)Peek16(0x03FE), (unsigned)Peek16(0x03F9),
                   (unsigned)Peek16(0x0406), (unsigned)Peek16(0x0408),
                   (unsigned)Peek16(0x0A90), (unsigned)Peek16(0x0A94),
                   (unsigned)Peek16(0x03ED), (unsigned)Peek16(0x0401),
                   (unsigned)Peek16(0x0405),
                   (unsigned)Peek16(0x00CE), (unsigned)Peek16(0x03E3),
                   (unsigned)Peek16(0x03E4), (unsigned)Peek16(0x03E5),
                   (unsigned)Peek16(0x03F1), (unsigned)Peek16(0x00B9));
            printf("        io01=%02X io04=%02X io07=%02X io0F=%02X $BE=%02X $C8=%02X $D1=%02X $C9=%02X\n",
                   nc2k_states.ram_io[0x01], nc2k_states.ram_io[0x04],
                   nc2k_states.ram_io[0x07], nc2k_states.ram_io[0x0f],
                   (unsigned)Peek16(0x00BE), (unsigned)Peek16(0x00C8),
                   (unsigned)Peek16(0x00D1), (unsigned)Peek16(0x00C9));
        }
    }
    tracing = false;
    io_access_counting = false;
    headless_io_cb = nullptr;
    nand_blk_counting = false;
    wlast_report();

    if (!save_flash_prefix.empty()) {
        void save_flash(string file);
        save_flash(save_flash_prefix);
        printf("saved flash to %s.nor / %s.nand\n",
               save_flash_prefix.c_str(), save_flash_prefix.c_str());
    }

    {
        uint32_t distinct = 0, total = 0;
        for (int i = 0; i < 4096; i++) if (nand_blk_read[i]) { distinct++; total += nand_blk_read[i]; }
        printf("NAND reads: %u page reads over %u distinct 16KB blocks\n", total, distinct);
        printf("first %d read requests:\n", nand_log_n);
        for (int i = 0; i < nand_log_n && i < 24; i++)
            printf("   #%02d tick=%-6u pc=%04X caller=%04X cmd=%02X addr=%02X %02X %02X %02X  page=%-6u block=%d\n",
                   i, nand_log[i].tick, nand_log[i].pc, nand_log[i].caller,
                   nand_log[i].cmd, nand_log[i].low,
                   nand_log[i].mid, nand_log[i].high, nand_log[i].a25,
                   nand_log[i].pos, nand_log[i].pos >> 5);
        printf("first %d writes to the NAND port ($39)  [value CLE ALE]:\n", nand_wlog_n);
        for (int i = 0; i < nand_wlog_n && i < 40; i++)
            printf("   %02X/%d%d@%04X%s", nand_wlog[i][0], nand_wlog[i][1], nand_wlog[i][2],
                   nand_wpc[i], ((i % 5) == 4) ? "\n" : "  ");
        printf("\n");
        for (int k = 0; k < 10; k++) {
            int best = -1; uint32_t bestv = 0;
            for (int i = 0; i < 4096; i++) if (nand_blk_read[i] > bestv) { bestv = nand_blk_read[i]; best = i; }
            if (best < 0 || !bestv) break;
            printf("   block %4d (0x%05X) : %u reads\n", best, best * 16384, bestv);
            nand_blk_read[best] = 0;
        }
    }

    /*
     * 纯模拟速度（2026-09-29 加）：headless 不做节流，所以这是"这台机器最快能跑多少倍"。
     * > 1.00 才有余量；如果在慢机器上 < 1.00，声音必然被拖慢 —— 声卡按墙上时间要样本，
     * 061 的样本却按模拟时间产，跟不上时 sound.cpp 只能补 0（听感就是音乐被拉长/发闷）。
     */
    {
        double wall_ms = (double)std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - g_headless_t0).count();
        printf("wall=%.0fms for %dms emulated  =>  speed=%.2fx realtime (headless, unpaced)\n",
               wall_ms, ms, wall_ms > 0 ? (double)ms / wall_ms : 0.0);
    }

    print_top("PC where IO 0x3A was written", pc_at_3a_w, 6, io_access_count[1][0x3a]);
    print_top("PC where IO 0x3C was read", pc_at_3c_r, 6, io_access_count[0][0x3c]);
    print_top("PC where IO 0x0E was read", pc_at_0e_r, 6, io_access_count[0][0x0e]);
    print_top("PC where IO 0x08 (port0) was read", pc_at_08_r, 8, io_access_count[0][0x08]);
    print_top("PC where IO 0x1E (port6) was read", pc_at_1e_r, 8, io_access_count[0][0x1e]);

    printf("IO port accesses (read/write):\n");
    for (int a = 0; a < 0x40; a++) {
        if (!io_access_count[0][a] && !io_access_count[1][a]) continue;
        printf("   $%02X r=%-8u w=%-8u %s\n", a, io_access_count[0][a], io_access_count[1][a],
               (a >= 0x30 && a <= 0x33) ? "<- NC2000 DSP port range" :
               (a >= 0x3a && a <= 0x3d) ? "<- UART (061 link)" : "");
    }

    int pages = 0;
    for (int i = 0; i < 0x100; i++) pages += page_seen[i] ? 1 : 0;
    printf("instructions=%llu  distinct PC pages=%d  BRK/INT=%llu\n",
           (unsigned long long)insn_count, pages, (unsigned long long)brk_count);
    printf("io: bank=%02X bios_bsw=%02X zp_bsw=%02X clk=%02X lcd_cfg=%02X "
           "lcd_ctrl=%02X lcd_seg=%02X keyport0=%02X rambsw=%02X exc=%02X\n",
           nc2k_states.ram_io[0x00], nc2k_states.ram_io[0x0a], nc2k_states.ram_io[0x0f],
           nc2k_states.ram_io[0x05], nc2k_states.ram_io[0x06], nc2k_states.ram_io[0x0b],
           nc2k_states.ram_io[0x0d], nc2k_states.ram_io[0x08], nc2k_states.ram_io[0x19],
           nc2k_states.ram_io[0x1a]);
    printf("io: port3(0x0e)=%02X port4(0x18)=%02X port5(0x3c)=%02X port6(0x1e)=%02X "
           "uart: 3a=%02X 3b=%02X 3c=%02X 3d=%02X (BSR=%02X LCR=%02X LSR=%02X IRCR=%02X "
           "MCR=%02X MSR=%02X bk=%02X)\n",
           nc2k_states.ram_io[0x0e], nc2k_states.ram_io[0x18], nc2k_states.ram_io[0x3c],
           nc2k_states.ram_io[0x1e], nc2k_states.ram_io[0x3a], nc2k_states.ram_io[0x3b],
           nc2k_states.ram_io[0x3c], nc2k_states.ram_io[0x3d],
           nc2k_states.BSR, nc2k_states.LCR, nc2k_states.LSR, nc2k_states.IRCR,
           nc2k_states.MCR, nc2k_states.MSR, nc2k_states.bk);
    if (!brks.empty()) {
        printf("first INT/BRK calls (pc -> idx bank):\n");
        for (size_t i = 0; i < brks.size() && i < 40; i++)
            printf("   %04X -> %02X %02X\n", brks[i].pc, brks[i].idx, brks[i].bank);
    }
    printf("INT call histogram (bank/idx : count):\n");
    for (int k = 0; k < 24; k++) {
        int best = -1; uint32_t bestv = 0;
        for (int i = 0; i < 0x10000; i++) if (int_hist[i] > bestv) { bestv = int_hist[i]; best = i; }
        if (best < 0 || !bestv) break;
        printf("   INT $%02X%02X : %u\n", best >> 8, best & 0xFF, bestv);
        int_hist[best] = 0;
    }

    bool grey = is_grey_mode();
    printf("grey_mode=%d\n", (int)grey);
    if (dump_ram_len) {
        FILE *f = fopen(dump_ram_path.c_str(), "wb");
        if (!f) { printf("cannot write %s\n", dump_ram_path.c_str()); }
        else {
            for (uint32_t k = 0; k < dump_ram_len; k++) {
                uint8_t v = Peek16((uint16_t)(dump_ram_start + k));
                fwrite(&v, 1, 1, f);
            }
            fclose(f);
            printf("dumped %u bytes from $%04X to %s\n",
                   dump_ram_len, dump_ram_start, dump_ram_path.c_str());
        }
    }
    if (!dump_all_ram_path.empty()) {
        FILE *f = fopen(dump_all_ram_path.c_str(), "wb");
        if (f) {
            fwrite(nc2k_states.ram, 1, sizeof(nc2k_states.ram), f);
            fclose(f);
            printf("dumped internal RAM (%u bytes) to %s\n",
                   (unsigned)sizeof(nc2k_states.ram), dump_all_ram_path.c_str());
        }
    }
    if (nand_win_taken) {
        std::string snap = dump_all_ram_path.empty() ? std::string("../out/nc3000/nandwin.bin")
                                                     : dump_all_ram_path + ".win4000";
        FILE *f = fopen(snap.c_str(), "wb");
        if (f) { fwrite(nand_win_snap, 1, sizeof(nand_win_snap), f); fclose(f);
                 printf("snapshot of the $4000-$BFFF window -> %s\n", snap.c_str()); }
    }
    if (snap_2000_taken && !dump_ram_path.empty()) {
        std::string snap = dump_ram_path + ".win2000";
        FILE *f = fopen(snap.c_str(), "wb");
        if (f) { fwrite(snap_2000, 1, sizeof(snap_2000), f); fclose(f);
                 printf("snapshot of the $2000-$3FFF window -> %s\n", snap.c_str()); }
    }
    nc3_dsp_stats();
    printf("irq stats: timebase_fired=%u irq_pending=%u irq_taken=%u timebase_gate_fail=%u\n",
           nc3_timebase_fired, nc3_irq_pending, nc3_irq_taken, nc3_timebase_gate_fail);
    for (int k = 0; k < count_n; k++)
        printf("count $%04X = %llu\n", count_tbl[k], (unsigned long long)count_val[k]);
    if(!dump_audio_path.empty()) dump_audio_end();
    if(!dump061_path.empty()) dump061_end();
    if (dump_lcd_bmp(dump.c_str(), grey))
        printf("wrote %s\n", dump.c_str());

    /* top PC pages, useful to tell "running the firmware" from "stuck" */
    printf("hot PC pages (exact):\n");
    for (int k = 0; k < 12; k++) {
        int best = -1; uint32_t bestv = 0;
        for (int i = 0; i < 0x10000; i++) if (pc_hist[i] > bestv) { bestv = pc_hist[i]; best = i; }
        if (best < 0 || bestv == 0) break;
        printf("   $%04X : %u insns (%.2f%%)\n", best, bestv,
               100.0 * bestv / (insn_count ? insn_count : 1));
        pc_hist[best] = 0;
    }
    return 0;
}
