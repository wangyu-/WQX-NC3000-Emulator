#include "comm.h"

extern uint8_t &RCR0;
extern uint8_t &RCR1;

extern int uart_log_level;
extern bool uart_advance;

const int RCR0_ALARM= 0x02;
const int RCR0_2HZ=   0x01;

/* ---- 红外（IrDA）/ 主机链路 ----
 * NC3000 的红外和 061 共用 $3A-$3D 这组 UART 寄存器，靠 IRCR（$3B，bk=1）
 * 分流：061 的初始化写 IRCR=0（bank0 $FBAE），红外的初始化写 IRCR=0x33
 * （bank16 $8A94）。IRCR != 0 ⇒ 这段时间 UART 归红外用。
 * 详见 iv_uart.cpp 里那段注释，以及 docs/NC3000_红外与DSP端口分流.md */
bool irda_uart_active(void);
void irda_send(uint8_t byte);      /* UART 发出的字节（模拟红外发射管） */
void irda_inject(uint8_t byte);    /* 给将来的主机桥：注入一个"收到"的字节 */
bool irda_rx_ready(void);
uint8_t irda_recv(void);

/* UART 归属（061 仅会话期间占用）：$0E bit3 的 /RESET 脉冲 = 061 会话开始 */
void uart_061_session_begin(void);

const int RCR1_SAMPLE= 0x04;
const int RCR1_ALARM= 0x02;
const int RCR1_2HZ=   0x01;

const int IV_2HZ = 0x00;
const int IV_SAMPLE= 0x01;
const int IV_ALARM= 0x02;
const int IV_NONE= 0x1f;
void clear_iv();
void put_iv(uint8_t value);
void del_iv(uint8_t value);
uint8_t peek_iv();
uint8_t get_iv();

uint8_t read_rcr0();
void write_rcr0(uint8_t value);

inline uint32_t get_sample_hz(){
    uint32_t high=RCR0>>4;
    if(high==0||high==0xf) return 0;
    return 1<<(high-1);
}

uint8_t read_rcr1();
void write_rcr1(uint8_t value);

uint8_t read_3a();
void write_3a(uint8_t value);

uint8_t read_3b();
void write_3b(uint8_t value);

uint8_t read_3c();
void write_3c(uint8_t value);

uint8_t read_3d();
void write_3d(uint8_t value);

void open_serial_port(char *port_name);
