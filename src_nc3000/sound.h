#pragma once
#include "comm.h"

void init_audio();
void shutdown_audio();

void reset_dsp();
void write_data_to_dsp(uint8_t, uint8_t);
void dsp_move(int len /*unit sample*/);

void beeper_on_io_write(int );
void post_cpu_run_sound_handling();

/* headless helpers: run the mixer without opening an SDL audio device */
void dump_audio_begin();
void dump_audio_pump(int frames);
void dump_audio_end();

/* NC3000: 061 -> 声卡之间的音频队列（main.cpp 用它做背压） */
int  dsp061_queue_len(void);
void dsp061_flush_queue(void);
