#include "dsp.h"

#include <cstdio>

static int g_dsp_log_level = 0;

void set_dsp_log_level(int level) {
    g_dsp_log_level = level;
}

void Dsp::reset() {
    dspMode = 0;
}

void Dsp::write(uint8_t high, uint8_t low) {
    (void)low;
    (void)high;
    /* no SPDS104A emulation available; NC3000 sound goes through SPCE061A */
}
