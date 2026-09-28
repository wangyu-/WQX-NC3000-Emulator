/* Host smoke test for the 061.dat emulator (spce061a.c + unsp.c).
 *
 * Checks the whole power-on path with the real peripheral model:
 *   reset 0xCB9F -> UART init -> handshake spin 0x823E -> (we answer 0xBB,0x0A)
 *   -> firmware replies 0xFABB on its UART -> main loop 0x8232
 *
 * usage: fw_smoke [b0 b1]        handshake bytes in hex, default "bb 0a"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "spce061a.h"

extern const uint16_t firmware_061[];
extern const uint32_t firmware_061_count;

#define FW_BASE 0x8200u
#define FW_END  0x10000u
#define RESET_ENTRY   0xCB9Fu
#define VECTOR_TABLE  0xFC00u
#define MAIN_LOOP     0x8232u
#define HS_LOOP       0x823Eu

#define MAX_STEPS 8000000u

static uint32_t pc_hist[0x10000 / 16];      /* coarse histogram, 16-word bins */
static uint8_t  tx_bytes[64];
static int      tx_n;
static int      got_main, injected;
static uint32_t main_step;
static uint32_t fiq_hits, audio_seen;

static void tx_hook(void *ctx, uint8_t b)
{
    (void)ctx;
    if (tx_n < (int)sizeof tx_bytes)
        tx_bytes[tx_n++] = b;
}

static const char *reg_name(uint16_t a)
{
    switch (a) {
    case 0x7005: return "P_IOB_Data";
    case 0x700A: return "P_TimerA_Data";
    case 0x700B: return "P_TimerA_Ctrl";
    case 0x700C: return "P_TimerB_Data";
    case 0x700D: return "P_TimerB_Ctrl";
    case 0x7010: return "P_INT_Ctrl";
    case 0x7011: return "P_INT_Clear";
    case 0x7013: return "P_SystemClock";
    case 0x7016: return "P_DAC2";
    case 0x7017: return "P_DAC1";
    case 0x7021: return "P_UART_Cmd1";
    case 0x7023: return "P_UART_Data";
    case 0x7024: return "P_UART_BaudLo";
    case 0x7025: return "P_UART_BaudHi";
    case 0x702A: return "P_DAC_Ctrl";
    case 0x702D: return "P_INT_Mask";
    default: return NULL;
    }
}

static void io_trace(void *ctx, uint16_t addr, uint16_t value)
{
    const char *n = reg_name(addr);
    (void)ctx;
    if (n)
        printf("    [io] %-16s (0x%04X) = 0x%04X\n", n, addr, value);
}

static uint32_t n_fiq_on, n_fiq_off, n_irq_on, n_irq_off;

static void cpu_trace(void *ctx, uint16_t pc, uint16_t op)
{
    (void)ctx; (void)pc;
    switch (op) {
    case 0xF14E: n_fiq_on++; break;
    case 0xF14C: n_fiq_off++; break;
    case 0xF149: n_irq_on++; break;
    case 0xF148: n_irq_off++; break;
    default: break;
    }
}

static void report_top_pcs(void)
{
    int i, k;
    printf("  hottest 16-word code bins (bin = base PC):\n");
    for (k = 0; k < 8; k++) {
        uint32_t best = 0, bi = 0;
        for (i = 0; i < (int)(sizeof pc_hist / sizeof pc_hist[0]); i++)
            if (pc_hist[i] > best) { best = pc_hist[i]; bi = i; }
        if (!best) break;
        printf("    0x%04X..0x%04X : %u\n", 0x8000 + bi * 16,
               0x8000 + bi * 16 + 15, (unsigned)best);
        pc_hist[bi] = 0;
    }
}

int main(int argc, char **argv)
{
    static spce_t m;
    int i;
    uint32_t step;
    unsigned b0 = 0xBB, b1 = 0x0A;

    if (argc > 2) {
        b0 = (unsigned)strtoul(argv[1], NULL, 16);
        b1 = (unsigned)strtoul(argv[2], NULL, 16);
    }
    spce_init(&m, firmware_061, FW_BASE, FW_END);
    for (i = 0; i < 11; i++)
        m.cpu.vec_area[i] = firmware_061[VECTOR_TABLE + i - FW_BASE];
    m.cpu.vec_loaded = 1;
    m.uart_tx = tx_hook;
    m.io_trace = io_trace;
    m.io_trace_ctx = NULL;
    m.cpu.trace = cpu_trace;
    m.cpu.trace_ctx = NULL;
    m.cpu.r[0] = 0x07FF;
    m.cpu.r[7] = RESET_ENTRY;

    printf("handshake bytes 0x%02X 0x%02X\n", b0, b1);
    for (step = 0; step < MAX_STEPS; step++) {
        spce_poll_irq(&m);
        if (m.cpu.r[7] < FW_BASE || m.cpu.r[7] >= FW_END) {
            printf("  pc left the ROM at step %u: 0x%04X\n",
                   (unsigned)step, m.cpu.r[7]);
            break;
        }
        pc_hist[(m.cpu.r[7] - 0x8000) >> 4]++;
        if (m.cpu.r[7] == 0xC742u)                  /* isr_fiq_audio entry */
            fiq_hits++;
        unsp_step(&m.cpu);
        spce_tick(&m, 1);
        /* answer the handshake once the firmware is actually asking for it */
        if (!injected && m.cpu.r[7] == HS_LOOP) {
            spce_uart_rx(&m, (uint8_t)b0);
            spce_uart_rx(&m, (uint8_t)b1);
            injected = 1;
            printf("  handshake bytes queued at step %u\n", (unsigned)step);
        }
        /* After the link ack the firmware (0x8222) waits for one more word:
         * 0x0FCC = DEBUG_CMD -> reply 0xCCFF and keep waiting, anything else
         * -> enter the main loop 0x8232.  We send START_MS01 (0x99 0x00). */
        if (injected == 1 && tx_n >= 2) {
            spce_uart_rx(&m, 0x99);
            spce_uart_rx(&m, 0x00);
            injected = 2;
            printf("  START_MS01 (99 00) queued at step %u\n", (unsigned)step);
        }
        if (m.cpu.r[7] == MAIN_LOOP) {
            got_main = 1;
            main_step = step;
            printf("reached main loop 0x8232 after %u instructions\n",
                   (unsigned)step);
            if (injected == 2) {          /* send it again for the real handler */
                spce_uart_rx(&m, 0x99);
                spce_uart_rx(&m, 0x00);
                injected = 3;
            }
            if (step > main_step + 2000000u)
                break;
        }
        if (spce_audio_avail(&m) > audio_seen)
            audio_seen = spce_audio_avail(&m);
    }
    printf("main loop reached: %d (step %u)\n", got_main, (unsigned)main_step);
    printf("firmware sent %d UART bytes:", tx_n);
    for (i = 0; i < tx_n; i++)
        printf(" %02X", tx_bytes[i]);
    printf("\n  P_IOB_Dir=%04X P_IOB_Attrib=%04X P_IOB_Data(latch)=%04X\n",
           m.port_b_dir, m.port_b_attrib, m.port_b_data);
    printf("  P_INT_Ctrl=%04X TimerA ctrl=%04X data=%04X count=%04X\n",
           m.int_ctrl, m.timer_a_ctrl, m.timer_a_data, m.timer_a_count);
    printf("  uart_cmd1=%04X rx pending=%d  ring head=%04X tail=%04X\n",
           m.uart_cmd1, m.uart_rx_head - m.uart_rx_tail,
           m.cpu.ram[0x0763], m.cpu.ram[0x0764]);
    printf("  audio FIQ entries=%u  DAC samples queued=%u\n",
           (unsigned)fiq_hits, (unsigned)audio_seen);
    printf("  FIQ on/off = %u/%u  IRQ on/off = %u/%u  now: fiq=%d irq=%d\n",
           (unsigned)n_fiq_on, (unsigned)n_fiq_off, (unsigned)n_irq_on,
           (unsigned)n_irq_off, m.cpu.enable_fiq, m.cpu.enable_irq);
    printf("  firmware sample rate = %u Hz (TimerA %04X reload %u)\n",
           (unsigned)spce_dac_sample_rate(&m), m.timer_a_data,
           (unsigned)(0x10000u - m.timer_a_data));
    if (!got_main)
        report_top_pcs();
    return got_main ? 0 : 1;
}
