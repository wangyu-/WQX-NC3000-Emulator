#include "nc2000.h"
#include "comm.h"
#include "ram.h"
#include "state.h"
#include "cpu.h"
#include "mem.h"
#include "io.h"
#include "rom.h"
#include "nor.h"
#include "nand.h"
#include <SDL2/SDL.h>
#include <SDL_timer.h>
#include <cassert>

#include <cstdio>
#include <cstdlib>
#include <deque>
#include "sound.h"

/* RunTimeSlice() 的时基补偿累加器（见该函数里的注释） */
static int64_t cycle_carry = 0;
#include "compare/c6502.h"
#include "console.h"
#include "iv_uart.h"
#include "cmd.h"
#include "key_new.h"
#include "spce061_bridge.h"
extern WqxRom nc2k_rom;

nc2k_states_t nc2k_states;

void save_state(string file_name){
	if(file_name.empty()) file_name=nc2k_rom.statesPath;
	else file_name+=".state";
	FILE* file = fopen(file_name.c_str(), "wb");
	if (file == NULL) {
		printf("states file %s open failed, skip saving!\n", nc2k_rom.statesPath.c_str());
		return;
	}
	fwrite(&nc2k_states.SAVE_STATE_BEGIN, 1, &nc2k_states.SAVE_STATE_END-&nc2k_states.SAVE_STATE_BEGIN, file);
	fflush(file);
	fclose(file);
	printf("state saved to file %s!!\n",file_name.c_str());
}

void delete_state(string file_name){
	if(file_name.empty()) file_name=nc2k_rom.statesPath;
	else file_name+=".state";
	if(remove(file_name.c_str())==0){
		printf("state file %s deleted!\n",file_name.c_str());
	}else{
		printf("state file %s not exist or no permission to delete!\n",file_name.c_str());
	}
}

void load_state(){
	FILE* file = fopen(nc2k_rom.statesPath.c_str(), "rb");
	if (file == NULL) {
		printf("states file %s open failed, skip loading!\n", nc2k_rom.statesPath.c_str());
		return;
	}
	int ret=fread(&nc2k_states.SAVE_STATE_BEGIN, 1, &nc2k_states.SAVE_STATE_END-&nc2k_states.SAVE_STATE_BEGIN, file);
	fclose(file);
	printf("loaded states from %s, ret=%d\n", nc2k_rom.statesPath.c_str(),ret);
	//super_switch();
}

void LoadNC2k(){
	nc2k_states.init(); //fix re-run issue on emscripten version
	clear_cmds();
	clear_nand_status();
	clear_iv();

	init_keyitems();

	init_io(); //for old io implemet only
	
	void CreateHotlinkMapping();
	CreateHotlinkMapping();

	init_nor();
	if(pc1000mode||nc1020mode) {
		init_rom();
	}
	if(nc2000mode||nc3000mode) {
		read_nand0_file();
		read_nand_file();
	}

	init_mem();

	if(nc1020mode){
		ram_io[0x0b]=0x01;
	}

	//reset_cpu_states();
	initalize_illegal_op_tables();
	init_cpu_new();

	if(nc2000mode||nc3000mode){
		//nc3000c-lee has it but seems like no need?
		//ram_io[0x18]=0x20;
	}

	if(nc3000mode){
		/* start the SPCE061A speech coprocessor before the main firmware runs:
		 * NC3000's BIOS talks to it during boot (0xBB handshake etc.) */
		nc3_dsp_boot();
	}

	if(enable_load_state){
		load_state();
		if(nc2000mode||nc3000mode){
			void sync_time_2000();
			if(enable_auto_time_sync) sync_time_2000();
		}
		if(nc1020mode){
			void sync_time_1020();
			if(enable_auto_time_sync) sync_time_1020();
		}
	}

	super_switch();

	if(enable_load_state&&reset_after_load_state){
		void set_warm_reset_flag();
		set_warm_reset_flag();
		void warm_reset();
		warm_reset();
	}
}

void SaveNC2kIfNeed(){
    if(save_flash_on_exit){
      save_flash("");
    }
    if(save_state_on_exit){
      save_state("");
    }
}

static unsigned short &lcdbuffaddr = nc2k_states.lcdbuffaddr;
static unsigned short &lcdbuffaddrmask = nc2k_states.lcdbuffaddrmask;
bool is_grey_mode(){
	if(console_on) return false;

	unsigned short lcd_addr = lcdbuffaddr&lcdbuffaddrmask;
	//printf("lcdaddr=%x\n",lcd_addr);
	//fflush(stdout);
	if(nc2000mode||nc3000mode||nc1020mode)
		return lcd_addr==0x1380;
	return false;
}
bool CopyLcdBuffer(uint8_t* buffer){
    unsigned short lcd_addr = lcdbuffaddr&lcdbuffaddrmask;
	if (lcd_addr == 0) return false;

	if(nc1020mode){
		if(!is_grey_mode()){
			memcpy(buffer, ram_buff + lcd_addr, 1600 );
		}else{
			memcpy(buffer, ram_buff + lcd_addr, 1600 *2);
		}
		return true;;
	}
	else if(nc2000mode||nc3000mode){
		/*
		 * 用户实测：NC3000 的显存极性**与 NC2000 相同**（不要再取反）。
		 * 之前加取反是因为用残缺 NOR dump 时固件跑不起来、画面本来就是错的，
		 * 误判成了极性反；换成完整 ROM 后取反反而变成"白字黑底"的反显。
		 * （真正的反显应该由固件的 LCD 方向位 io_lcd_diction(IO 0x10) 决定，
		 *   以后如果需要再按那个位来实现，这里先按实测置为不取反。）
		 */
		bool invert = false;
		if(!is_grey_mode()){
			//TODO: cannot use lcd_addr, it has some offset
			//// 应该是lcdaddr io哪里没有模拟好。 如果lcd end addr不是(1fff,0fff,...)可能应该忽略lcd_addr的设置
			memcpy(buffer, ram_buff + 0x19c0, 1600 );
			if (invert) for (int i = 0; i < 1600; i++) buffer[i] = (uint8_t)~buffer[i];
		}else{
			memcpy(buffer, ram_buff + lcd_addr, 1600 *2);
			if (invert) for (int i = 0; i < 1600 * 2; i++) buffer[i] = (uint8_t)(0xFF - buffer[i]);
		}
		return true;
	}else{
		memcpy(buffer, ram_buff + lcd_addr, 1600);
		return true;
	}
	assert(false);
}

void RunTimeSlice(uint32_t time_slice) {
	uint32_t new_cycles = time_slice * CYCLES_MS;

	if(!fast_forward) {
		new_cycles= new_cycles * speed_multiplier;
	}else if(fast_forward_limit==0){
		new_cycles= new_cycles*1;
	}else{
		new_cycles= new_cycles * fast_forward_limit;
	}

	/*
	 * 主控时基补偿（见 docs/NC3000模拟器_改造计划.md 第 14 节）：
	 * cpu_run3() 是按 cpu_batch(=64 周期)一批执行的，RunTimeSlice(1) 想要 10240 周期，
	 * 但"凑够就停"的写法每片平均会多走 ~25 周期（实测一片 = 10265 周期，
	 * 也就是模拟出来的时间比真实时间快 0.24%，长时间跑时钟会偏）。
	 * 这里把上一片多走的周期从这一片里扣掉，长期速率就精确等于 CYCLES_MS。
	 */
	if (cycle_carry > 0) {
		uint64_t sub = (cycle_carry > (int64_t)new_cycles) ? new_cycles : (uint64_t)cycle_carry;
		new_cycles -= (uint32_t)sub;
	} else if (cycle_carry < 0) {
		new_cycles += (uint32_t)(-cycle_carry);
	}
	if (new_cycles == 0) new_cycles = 1;      /* 最少跑一点，免得死循环 */

	u64_t target_cycles=nc2k_states.cycles +new_cycles;
	u64_t cycles_at_entry = nc2k_states.cycles;

	while (nc2k_states.cycles < target_cycles && !reload_pending) {
		if(cpu_loop_version == CPU_RUN1){
			cpu_run();
		}else if (cpu_loop_version == CPU_RUN2){
			cpu_run2();
		}else if (cpu_loop_version == CPU_RUN3){
			cpu_run3();
		}else{
			assert(false);
		}
		post_cpu_run_sound_handling();
	}

	cycle_carry = (int64_t)(nc2k_states.cycles - cycles_at_entry) - (int64_t)new_cycles;

	if(nc3000mode){
		/* Keep the 061 in step with the main CPU: its crystal is 49.152 MHz
		 * against the SPDC1064's 14.7456 MHz, so it needs **10/3** instructions per
		 * main-CPU cycle (49.152 / 14.7456 = 10/3 exactly) for the audio FIQ and the
		 * decoder to run at the real chip's rate.
		 *
		 * ⚠️ 2026-09-27：这里原来是 ×5（把 4.8 四舍五入了），结果 061 每秒多产 5%
		 * 的音频样本（实测 66,996/s vs DAC 的 63,833/s），多出来的全堆在主机侧队列里
		 * （24000 样本 ≈ 0.4s 满仓）。退出游戏时主控已经发了停止命令（0xAA 00），
		 * 但队列里那堆样本还在放，而且停止后 TimerA 改回 8 kHz、重采样比例变了 8 倍，
		 * 听起来就是"退出后一段延迟 + 怪声"。改成精确的 4.8（= 24/5）后产耗持平。
		 * Additional time is handed to it from inside the UART polling
		 * (nc3_dsp_rx_ready), so a slow 061 never deadlocks the firmware. */
		u64_t ran = nc2k_states.cycles - cycles_at_entry;
		nc3_dsp_run((int)(ran * 10 / 3));
	}
}

void save_flash(string file){
	write_nand0_file(file);
	write_nand_file(file);
	SaveNor(file);
	printf("flash saved to file!!\n");
}

void nc2k_warm_reset(){

	uint8_t* ioReg=nc2k_states.ram_io;

	const int simple_warm_reset=true;
  if(simple_warm_reset){
	ioReg[0x05] &=0x1f;  //reset cks
	nc2k_states.speed_scaledown=1;
  } else {
    //0x00
    ioReg[0x00]=0;

    //0x01
    nc2k_states.inner_interrupt_control&=0xfc; //TMBIE TMAIE clear
    
    //0x02
    ////ioReg[0x02]=0;          //if reset both 0x02 and 0x03, on/off key will trigger cold reset
    
    //0x03
    ////ioReg[0x03]=0;

    //0x04
    nc2k_states.w04_b03_TBC &=0xf0;
    
    //0x05
    ioReg[0x05] &=0x1f;
    ioReg[0x05] &=0xf7;
    nc2k_states.lcdon=0;
    nc2k_states.speed_scaledown=1;

    //0x0a
    ioReg[0x0a] &=0xe0;
    
    //0x0b
    ioReg[0x0b]&=0xfd;
    nc2k_states.lcden=0;

    //0x0c
    ioReg[0x0c]&=0xfc;
    nc2k_states.w0c_b67_TMODESL =0;
    nc2k_states.w0c_b45_TM0S =0;
    nc2k_states.w0c_b23_TM1S = 0;
    nc2k_states.w0c_b345_TMS = 0;

    //0x0d
    ioReg[0x0d]&=0xf8;

    //0x14
    ioReg[0x14]=0;

    //0x19
    ioReg[0x19]=0xef;

    //0x1a
    ioReg[0x1a]=0;

    //0x1b
    ioReg[0x1b]=0;

    //0x1c
    ioReg[0x1c]&=0xbf;

    //0x1e
    ioReg[0x1e]=0;
  }

    super_switch();
}

void nc2k_cold_reset(){
    clear_cmds();
    clear_nand_status();
    clear_iv();//if this is put into warm_reset, alarm wakeup will not work correcly

    nc2k_warm_reset();

    //memset(ram_io,0,sizeof(nc2k_states.ram_io));
    //memset(ext_reg, 0, sizeof(nc2k_states.ext_reg));
    nc2k_states.reset();
    super_switch();
}
