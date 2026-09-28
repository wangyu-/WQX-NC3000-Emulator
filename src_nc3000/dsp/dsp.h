#pragma once

/*
=====================================================================
 SPDS104A DSP stub
=====================================================================
 The upstream NC2000 emulator references a `dsp/dsp.cpp` (Lee's
 Pc1000emux derived SPDS104A core) that is *not* published with the
 source drop.  NC3000 does not use the SPDS104A at all - its speech
 coprocessor is an SPCE061A reachable over the main CPU's UART
 (IO 0x3A-0x3D), which is emulated separately in `spce061/`.

 So this file only has to keep the legacy nc2000/nc1020/pc1000 code
 paths compiling and audio-less.  Everything the shim has to expose
 was reconstructed from the call sites in sound.cpp / io_new.cpp /
 compare/pc1000bus.cpp / settings.cpp.
=====================================================================
*/

#include <cstdint>

class Dsp {
public:
    /* 0 = idle, 1/2 = A1600-ish modes, 4 = special (see io_new.cpp) */
    int dspMode = 0;

    /* called with interleaved little endian 16 bit samples */
    void (*callback)(unsigned char *p, int len) = nullptr;

    void reset();
    void write(uint8_t high, uint8_t low);
};

void set_dsp_log_level(int level);
