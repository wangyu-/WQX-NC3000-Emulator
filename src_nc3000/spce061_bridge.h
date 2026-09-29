#pragma once
/*
 =====================================================================
  SPCE061A bridge - the NC3000's speech coprocessor
 =====================================================================
  NC3000 does not use the NC2000's SPDS104A.  Its coprocessor is an
  SPCE061A wired to the main chip's UART:

      main chip IO 0x3A   data          (write = send to 061, read = byte from 061)
      main chip IO 0x3B   status        bit5/6 = transmitter ready
      main chip IO 0x3C   status        bit4   = a byte arrived
      main chip IO 0x3D   bank select / control
      main chip IO 0x0E   bit4 = 061 busy line (1 = busy, input)
                          bit3 = 061 /RESET   (output, active low): the BIOS pulses it
                                 (AND #$F7 -> STA $0E, delay, ORA #$08 -> STA $0E) before
                                 every DSP session; the 061 then re-runs its reset path,
                                 which is the only way `CC 0F` is answered with `CC FF`.
                                 Modelled in io_new.cpp (`case 0x0e` -> nc3_dsp_boot()).

  Everything below is the glue between that register model and the
  emulated 061 (`spce061a/`).  The 061 side speaks the protocol that
  `spce061a/host/nc3000_dsp.c` implements.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* reset the 061 and run its boot sequence; 1 = ok */
int      nc3_dsp_boot(void);
/* 1 when the emulated 061 drives its ready line low (host may send) */
int      nc3_dsp_ready(void);
int      nc3_dsp_rx_ready(void);
uint8_t  nc3_dsp_read(void);
int      nc3_dsp_write(uint8_t byte);
/* let the 061 execute; also advances its peripheral/audio clocks */
void     nc3_dsp_run(int steps);
/* error counters / diagnostics */
void     nc3_dsp_stats(void);

/* NC3_PERF（慢机器排查"声音被拖慢"用）：累计"跑 061"用掉的墙上时间（微秒）。
 * 由 nc3_dsp_run() 自己累加，配合 main.cpp 每秒一行的 [perf] 报表一起看。 */
extern double nc3_dsp_busy_us;
/* pull decoded audio (0x8000 centred, already offset to int16) */
int      nc3_dsp_audio(int16_t *out, int n);
/* the DAC sample rate the 061 firmware is currently running at (Hz) */
uint32_t nc3_dsp_sample_rate(void);

/* --- debug: drive the 061 with a known-good S600 frame stream -------------
 * Used to verify the audio plumbing (061 -> bridge -> SDL mixer) without
 * needing the main firmware to reach a "pronounce" path.  `data` must be a
 * whole number of 18 byte frames.  Feed it one frame every 24 emulated ms by
 * calling nc3_dsp_play_tick() once per emulated millisecond. */
int      nc3_dsp_play_begin(const uint8_t *data, uint32_t len);
void     nc3_dsp_play_tick(void);
int      nc3_dsp_play_active(void);

#ifdef __cplusplus
}
#endif
