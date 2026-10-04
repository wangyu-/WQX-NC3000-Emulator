#include "comm.h"

/*
 * On NC3000 this UART block (0x3A-0x3D) is the link to the SPCE061A speech
 * coprocessor - see spce061_bridge.h.
 *
 * NOTE (2026-09-29): 这里以前写着"这个 UART 没接到 PC/红外口" —— 那句是错的，已删。
 * 上游作者 wangyu- 反馈红外通信一切正常，红外那条路是通的，别再引用旧说法。
 */
#include "spce061_bridge.h"
#define NC3_LINK() (nc3000mode)
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>
#include "iv_uart.h"
#include "state.h"
#include "misc/irda_link.h"
#include <sys/types.h>
#include <deque>
#include <cstdlib>
using namespace std;

int uart_log_level=0;
bool uart_advance=0;

extern nc2k_states_t nc2k_states;
static uint8_t * ram_io=nc2k_states.ram_io;
static uint8_t * ext_reg=nc2k_states.ext_reg;

static uint8_t &bk=nc2k_states.bk;

set<uint8_t> iv_set;
uint8_t &RCR0=ext_reg[0x0a];
uint8_t &RCR1=ext_reg[0x0b];

void clear_iv(){
    iv_set.clear();
}
void put_iv(uint8_t value){
    assert(value!=IV_NONE);
    iv_set.insert(value);
}
void del_iv(uint8_t value){
    assert(value!=IV_NONE);
    iv_set.erase(value);
}
uint8_t peek_iv(){
    if(iv_set.empty()) return IV_NONE;
    return *iv_set.begin();
}
uint8_t get_iv(){
    if(iv_set.empty()) return IV_NONE;
    uint8_t value=*iv_set.begin();
    //iv_set.erase(iv_set.begin()); //interrupt vectors are not self clear?
    return value;
}

uint8_t read_rcr0(){
    return RCR0;
}

/* ==================== 红外（IrDA）/ 主机链路 ====================
 * NC3000 的红外和 061 语音**共用这组 UART 寄存器**（$3A-$3D）：$3D 的低 2 位选
 * 寄存器 bank，$3A/$3B/$3C 在不同 bank 下分别是数据/LSR/IRCR/...。
 *
 * 怎么区分"现在归谁用" —— 靠 IRCR（$3B，bk=1），两边的初始化代码写死了不同的值：
 *   061 语音 ：bank0 $FB8C 起，显式写 IRCR = **0**（$FBAE: `LDA #$00 / STA $3B`）
 *   红外     ：bank16 $8A56 起，写 IRCR = **0x33**（$8A94: `LDA #$33 / STA $3B`）
 * 所以 IRCR != 0 ⇒ 这段 UART 归红外；061 每次会话开始都会重写 IRCR=0 拿回去，
 * 不需要额外的"归还"钩子（自洽）。
 *
 * 老实现把 $3A-$3D 无条件接到 061 ⇒ 自测第 11 项 `irda` 拿 061 的协议字节去和
 * "自己刚发出去的字节"比较（bank16 $8A35：发 Y → 收 → CMP $3000,Y），必然对不上。
 * 实测改动前 `call1 0c 10` 的 $03F0 = 0（失败）。
 *
 * NC3_IR_LOOPBACK=1：把发出去的字节回灌给接收端（= 真机自测那种"自己收自己"，
 * 也是没有对手设备时的自检方式）；默认不开，就相当于真机红外前面什么都没有。
 */
static deque<uint8_t> irda_rx_q;
static unsigned long irda_tx_count = 0;

/* 主机串口后端（--uart-passthrough）：实现在本文件下面的平台分支里。
 * 红外那条路会同时喂给主机串口（nc3000 的 IRCR 分流把 UART 交给红外时用得上）。 */
void serial_port_write_byte(uint8_t byte);
void serial_port_poll_incoming(void);
bool serial_port_is_open(void);

/* ==================== UART 归属：061 / 红外 / 主机 ====================
 * nc3000 的 $3A-$3D 被三方共用（见 docs/NC3000_红外与DSP端口分流.md）：
 *   061 语音、红外（IRCR 模式）、有线/红外**通讯**（bank1 $63A2 发 / $6394 收，裸 UART）。
 * 旧实现把所有非 IrDA 的字节都塞给 061 ⇒ "通讯管理 → 有线通讯"的探针（实测 $3A <- C0）
 * 进了 061，ftplink 自然连不上。
 *
 * 现在按"会话"分（NC3_UART_OWNER=auto|061|host 可覆盖，默认 auto）：
 *   auto: 061 只在会话进行中占有 UART ——
 *     会话开始 = /RESET 脉冲（io_new.cpp $0E bit3 → nc3_dsp_boot()）或主控发出 0xBB（握手）
 *     会话结束 = 主控发出 0xAA（stop 0xAA 00 / 睡眠 0xAA 01）
 *     其余时间 UART 归"主机"= 串口(--uart-passthrough) + UDP 桥(NC3_IR_UDP) + 本地回环
 *   IRCR != 0 时永远优先走红外（也就是同一条主机路）。
 */
static int  uart_owner_mode = -1;        /* 0=host 1=061 2=auto */
static bool uart_061_session = false;
static uint64_t uart_061_last_cyc = 0;
static bool uart_061_aa_pending = false;   /* 见到 0xAA 后，让它后面那个参数字节也归 061 */
#define UART_061_IDLE_MS 5000            /* 兜底：这么久没有 061 数据 ⇒ 会话结束 */

static int uart_owner_mode_get(){
    if(uart_owner_mode < 0){
        const char *e = getenv("NC3_UART_OWNER");
        if(e && (e[0]=='0' || e[0]=='h' || e[0]=='H')) uart_owner_mode = 0;
        else if(e && (e[0]=='1' || e[0]=='6'))          uart_owner_mode = 1;
        else                                            uart_owner_mode = 2;
        printf("[uart] owner mode = %s\n",
               uart_owner_mode==0 ? "host (串口/UDP，等于上游行为，061 收不到字节)" :
               uart_owner_mode==1 ? "061 (旧行为)" : "auto (061 仅会话期间)");
    }
    return uart_owner_mode;
}
static bool uart_061_active(){
    int m = uart_owner_mode_get();
    if(m == 1) return true;
    if(m == 0) return false;
    if(uart_061_session){
        uint32_t div = CYCLES_MS ? (uint32_t)CYCLES_MS : 1;
        uint32_t now_ms  = (uint32_t)(nc2k_states.cycles / div);
        uint32_t last_ms = (uint32_t)(uart_061_last_cyc / div);
        if(now_ms - last_ms > UART_061_IDLE_MS){
            uart_061_session = false;
            if(uart_log_level>=1)
                printf("[uart] 061 session end (idle > %d ms) -> UART 归主机\n", UART_061_IDLE_MS);
        }
    }
    return uart_061_session;
}
/* 061 的 $0E bit3 /RESET 脉冲 = 会话开始（io_new.cpp 调） */
void uart_061_session_begin(){
    if(uart_owner_mode_get() == 0) return;
    if(!uart_061_session && uart_log_level>=1) printf("[uart] 061 session begin (/RESET)\n");
    uart_061_session = true;
    uart_061_last_cyc = nc2k_states.cycles;
}
static bool uart_goes_to_061(){
    if(!nc3000mode) return false;          /* 其它机型没有 061，UART 归串口 */
    if(irda_uart_active()) return false;   /* 红外优先 */
    return uart_061_active();
}

/* 发一个字节时**按字节**决定归属（会话外收到 0xBB 握手 ⇒ 重新判给 061）：
 *   0xBB      = 061 握手，任何时候都算"会话开始"（061 唤醒后必须重新握手）
 *   0xAA 01   = 061 休眠 ⇒ 会话结束，UART 交回主机（红外/串口/UDP）
 *   0xAA 00   = 061 停止，会话继续
 *   其余      = 会话中归 061；会话外归主机 */
static bool uart_tx_byte_to_061(uint8_t b){
    int m = uart_owner_mode_get();
    if(m == 0) return false;
    if(m == 1) return true;
    if(irda_uart_active()) return false;
    if(uart_061_session){
        uart_061_last_cyc = nc2k_states.cycles;
        if(b == 0xAA){
            uart_061_aa_pending = true;
        }else if(uart_061_aa_pending){
            uart_061_aa_pending = false;
            if(b == 0x01){
                uart_061_session = false;
                if(uart_log_level>=1) printf("[uart] 061 session end (0xAA 01 sleep) -> UART 归主机\n");
            }
        }
        return true;
    }
    if(b == 0xBB){
        uart_061_aa_pending = false;
        uart_061_session = true;
        uart_061_last_cyc = nc2k_states.cycles;
        if(uart_log_level>=1) printf("[uart] 061 session begin (0xBB handshake)\n");
        return true;
    }
    return false;
}

static bool irda_loopback_enabled(){
    static int v = -1;
    if(v < 0){
        const char *e = getenv("NC3_IR_LOOPBACK");
        v = (e && atoi(e)) ? 1 : 0;
    }
    return v != 0;
}
bool irda_uart_active(){
    return (nc2k_states.IRCR != 0);
}
void irda_send(uint8_t byte){
    irda_tx_count++;
    if(uart_log_level>=1) printf("[irda] tx %02X (n=%lu%s)\n", byte, irda_tx_count,
                                 irda_loopback_enabled()?", loopback":"");
    irda_link_send(&byte, 1);                  /* 主机桥：把 IR 字节发到 UDP 对端 */
    serial_port_write_byte(byte);              /* 也喂给 --uart-passthrough 的串口 */
    if(irda_loopback_enabled()) irda_rx_q.push_back(byte);
}
/* 给将来的主机桥（串口/UDP/另一台模拟器）用：往接收队列塞一个字节 */
void irda_inject(uint8_t byte){
    irda_rx_q.push_back(byte);
}
bool irda_rx_ready(){
    irda_link_poll();                          /* 主机桥：把 UDP 收到的字节灌进接收队列 */
    serial_port_poll_incoming();               /* 串口收到的字节也灌进来 */
    return !irda_rx_q.empty();
}
uint8_t irda_recv(){
    if(irda_rx_q.empty()) return 0xff;
    uint8_t v = irda_rx_q.front();
    irda_rx_q.pop_front();
    if(uart_log_level>=1) printf("[irda] rx %02X\n", v);
    return v;
}
void write_rcr0(uint8_t value){
    if(debug_level>=1){
        printf("write_rcr0 %02x\n",value);
    }
    RCR0=value;
}

uint8_t read_rcr1(){
    return RCR1;
}
void write_rcr1(uint8_t value){
    if(value & RCR1_ALARM) del_iv(IV_ALARM);
    if(value & RCR1_2HZ) del_iv(IV_2HZ);
    if(value & RCR1_SAMPLE) del_iv(IV_SAMPLE);
    //the 3 low bits are self clear
    RCR1= value&0xf8;
}

/*
====================
uart host dev handle
====================
*/
#if defined(__MINGW32__)
/* ==========================================================================
 * 主机串口后端（Windows 原生，不依赖 libserialport）
 *
 * 作用跟上游 wangyu-/NC2000 的 --uart-passthrough 一样：把电脑上的串口
 * 透传给文曲星的 UART（红外和串口在机器侧是同一个口）。
 *   .\nc3000.exe ... --uart-passthrough COM3
 * 读是非阻塞的（ReadIntervalTimeout=MAXDWORD ⇒ 没数据立刻返回），
 * 用 ClearCommError() 的 cbInQue 判断"有没有数据"，跟 libserialport 那版的
 * sp_input_waiting() 语义一致。
 *
 * 注意：nc3000 模式下 $3A-$3D 平时归 061，只有固件进红外（IRCR != 0，
 * 见 irda_uart_active()）时才由红外那条路接管；红外那条路会把字节同时喂给
 * 串口和 UDP 桥，所以 --uart-passthrough 和 NC3_IR_UDP 可以一起用。
 * ========================================================================== */
#include <windows.h>

void handle_uart_parameter_change();      /* 定义在本分支末尾 */

#ifndef ONESTOPBITS
#define ONESTOPBITS 0                     /* MinGW 的 winbase.h 有些版本没给这个别名 */
#endif

static HANDLE serial_handle = INVALID_HANDLE_VALUE;
static int current_baudrate = 115200;
static int current_wordlen  = 8;
static int current_stopbits = 1;
static int current_parity   = 0;        /* 0=none 1=odd 2=even 3=mark 4=space */

static bool serial_open(){ return serial_handle != INVALID_HANDLE_VALUE; }

bool serial_port_is_open(){ return serial_open(); }

static void serial_write_raw(uint8_t byte){
    DWORD written = 0;
    if(!WriteFile(serial_handle, &byte, 1, &written, NULL) && uart_log_level>=1){
        printf("[uart] WriteFile failed (GetLastError=%lu)\n", GetLastError());
    }
}
static bool serial_read_raw(uint8_t *out){
    DWORD got = 0;
    if(!ReadFile(serial_handle, out, 1, &got, NULL) || got != 1) return false;
    return true;
}
static bool serial_in_que(){
    if(!serial_open()) return false;
    COMSTAT st; DWORD errs = 0;
    if(!ClearCommError(serial_handle, &errs, &st)) return false;
    return st.cbInQue > 0;
}

void serial_port_write_byte(uint8_t byte){
    if(serial_open()) serial_write_raw(byte);
}
void serial_port_poll_incoming(void){
    if(!irda_uart_active() || !serial_open()) return;
    for(int i = 0; i < 64; i++){
        if(!serial_in_que()) break;
        uint8_t b = 0xff;
        if(!serial_read_raw(&b)) break;
        irda_inject(b);
    }
}

void open_serial_port(char *port_name){
    char path[128];
    if(strncmp(port_name, "\\\\.\\", 4) == 0) snprintf(path, sizeof path, "%s", port_name);
    else snprintf(path, sizeof path, "\\\\.\\%s", port_name);   /* COM10 以上必须这么写 */
    HANDLE h = CreateFileA(path, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(h == INVALID_HANDLE_VALUE){
        printf("[uart] open %s failed (GetLastError=%lu)\n", path, GetLastError());
        return;
    }
    serial_handle = h;
    COMMTIMEOUTS to;
    memset(&to, 0, sizeof to);
    to.ReadIntervalTimeout = MAXDWORD;      /* 有就返回，没有立刻返回 0 */
    to.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(serial_handle, &to);
    PurgeComm(serial_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    current_baudrate = 115200; current_wordlen = 8; current_stopbits = 1; current_parity = 0;
    handle_uart_parameter_change();          /* 按固件当前 BSR/LCR 设一遍（要 --uart-advance）*/
    printf("[uart] passthrough %s opened\n", path);
}

bool is_write_ready() {
    if(uart_goes_to_061()) return true;
    if(NC3_LINK()) return true;                 /* 红外/主机路：随时可写 */
    return serial_open();
}
bool is_read_ready() {
    if(uart_goes_to_061()) return nc3_dsp_rx_ready();
    if(NC3_LINK()) return irda_rx_ready();      /* 红外/主机：串口 + UDP + 回环 */
    return serial_in_que();
}
void write_one_byte(uint8_t byte) {
    if(uart_tx_byte_to_061(byte)) { nc3_dsp_write(byte); return; }
    if(NC3_LINK()) { irda_send(byte); return; }
    if(!serial_open()) return;
    serial_write_raw(byte);
}
uint8_t read_one_byte() {
    if(uart_goes_to_061()) return nc3_dsp_read();
    if(NC3_LINK()) return irda_recv();
    if(!serial_open()) return 0xff;
    uint8_t b = 0xff;
    if(!serial_read_raw(&b)) return 0xff;
    return b;
}
void clear_read_buffer(const char *hint){
    if(!serial_open()) return;
    PurgeComm(serial_handle, PURGE_RXCLEAR);
    if(uart_log_level>=2) printf("[uart] clear read buffer (%s)\n", hint ? hint : "");
}
void handle_uart_parameter_change(){
    if(!uart_advance) return;
    if(!serial_open()) return;
    int baud_code = nc2k_states.BSR & 0x0f;
    if(baud_code > 12) baud_code = 12;
    static const int baud_tab[13] = {230400,115200,57600,38400,19200,9600,4800,
                                     2400,1200,600,300,150,75};
    int baud = baud_tab[baud_code];
    int wordlen  = (nc2k_states.LCR & 0x01) ? 8 : 7;
    int stopbits = (nc2k_states.LCR & 0x02) ? 2 : 1;
    int paritybits = (nc2k_states.LCR >> 2) & 0x7;
    BYTE parity = NOPARITY;
    if(paritybits == 1) parity = ODDPARITY;
    else if(paritybits == 3) parity = EVENPARITY;
    else if(paritybits == 5) parity = MARKPARITY;
    else if(paritybits == 7) parity = SPACEPARITY;
    else if(paritybits != 0 && uart_log_level>=1) printf("[uart] unsupported parity %d -> none\n", paritybits);
    if(baud == current_baudrate && wordlen == current_wordlen &&
       stopbits == current_stopbits && (int)parity == current_parity) return;
    DCB dcb;
    memset(&dcb, 0, sizeof dcb);
    dcb.DCBlength = sizeof dcb;
    if(!GetCommState(serial_handle, &dcb)){
        printf("[uart] GetCommState failed (GetLastError=%lu)\n", GetLastError());
        return;
    }
    dcb.BaudRate = (DWORD)baud;
    dcb.ByteSize = (BYTE)wordlen;
    dcb.Parity   = parity;
    dcb.StopBits = (stopbits == 2) ? TWOSTOPBITS : ONESTOPBITS;
    dcb.fBinary = TRUE;
    dcb.fParity = (parity != NOPARITY);
    dcb.fOutxCtsFlow = FALSE; dcb.fOutxDsrFlow = FALSE; dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE; dcb.fInX = FALSE; dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE; dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if(!SetCommState(serial_handle, &dcb)){
        printf("[uart] SetCommState failed (GetLastError=%lu)\n", GetLastError());
        return;
    }
    current_baudrate = baud; current_wordlen = wordlen;
    current_stopbits = stopbits; current_parity = (int)parity;
    if(uart_log_level>=1) printf("[uart] %d %dN%d%s\n", baud, wordlen, stopbits,
                                 parity ? " (parity on)" : "");
}
#elif defined (ENABLE_SERIAL_PORT)
#include <libserialport.h>
static int check(enum sp_return result)
{
    char *error_message;
    switch (result) {
    case SP_ERR_ARG:
        printf("libserialport Error: Invalid argument.\n");
        exit(-1);
    case SP_ERR_FAIL:
        error_message = sp_last_error_message();
        printf("libserialport Error: Failed: %s\n", error_message);
        sp_free_error_message(error_message);
        exit(-1);
    case SP_ERR_SUPP:
        printf("libserialport Error: Not supported.\n");
        exit(-1);
    case SP_ERR_MEM:
        printf("libserialport Error: Couldn't allocate memory.\n");
        exit(-1);
    case SP_OK:
    default:
        return result;
    }
}
struct sp_port *uart_port=nullptr;
int current_baudrate = 115200;
int current_wordlen = 8;
enum sp_parity current_parity = SP_PARITY_NONE;
int current_stopbits = 1;

void open_serial_port(char *port_name){
    printf("Looking for port %s.\n", port_name);
    check(sp_get_port_by_name(port_name, &uart_port));

    printf("Opening port.\n");
    check(sp_open(uart_port, SP_MODE_READ_WRITE));

    //my_baudrate=9600;
    printf("Setting port to %d 8N1, no flow control.\n", current_baudrate);
    check(sp_set_baudrate(uart_port, current_baudrate));
    check(sp_set_bits(uart_port, current_wordlen));
    check(sp_set_parity(uart_port, current_parity));
    check(sp_set_stopbits(uart_port, current_stopbits));
    check(sp_set_flowcontrol(uart_port, SP_FLOWCONTROL_NONE));
}
bool is_write_ready() {
    /* $3B bit5/6: as far as the firmware is concerned the transmit holding
     * register is empty; flow control for the 061 is IO 0x0E bit4. */
    if(uart_goes_to_061()) return true;
    if(NC3_LINK()) return true;
    if(!uart_port) return false;
    int waiting = check(sp_output_waiting(uart_port));
    if (waiting < 0) {
        assert(false);
    }
    return (waiting == 0); 
}

bool is_read_ready() {
    if(uart_goes_to_061()) return nc3_dsp_rx_ready();
    if(NC3_LINK()) return irda_rx_ready();
    if(!uart_port) return false;
    int bytes_waiting = check(sp_input_waiting(uart_port));
    if (bytes_waiting < 0) {
        assert(false);
    }
    return bytes_waiting>0;
}
void write_one_byte(uint8_t byte) {
    if(uart_tx_byte_to_061(byte)) { nc3_dsp_write(byte); return; }
    if(NC3_LINK()) { irda_send(byte); return; }
    if(!uart_port) return ;
    if(!is_write_ready()){
        if(uart_log_level>=1) printf("uart write but not ready\n");
        return ;
    }
    //on some windows, timeout 1ms or sp_nonblocking_write doesn't work, even if is_write_ready() is true.
    //here use 1000ms for compatibility
    unsigned int timeout_ms = 1000;
    if(uart_log_level>=2) printf("write one byte %02x , write pedning=%d\n",byte, sp_output_waiting(uart_port));
    int result = check(sp_blocking_write(uart_port, &byte, 1, timeout_ms));
    assert(result==1);
}

uint8_t read_one_byte() {
    if(uart_goes_to_061()) return nc3_dsp_read();
    if(NC3_LINK()) return irda_recv();
    if(!uart_port) return 0xff;
    if(!is_read_ready()){
        if(uart_log_level>=1) printf("uart read but not ready\n");
        return 0xff;
    }
    unsigned int timeout_ms=1000;
    unsigned char buf[2];
    int result = check(sp_blocking_read(uart_port, buf, 1, timeout_ms));
    assert(result==1);
    if(uart_log_level>=2) printf("read one byte %02x, read pending=%d\n",buf[0], sp_input_waiting(uart_port));
    return buf[0];
}

void clear_read_buffer(const char *hint){
    if(!uart_port) return ;
    int cnt=0;
    while(is_read_ready()){
        cnt++;
        unsigned char buf[2];
        unsigned int timeout_ms=1000;
        int result = check(sp_blocking_read(uart_port, buf, 1, timeout_ms));
        assert(result==1);
    }
    if(cnt>0){
        if(uart_log_level>=2) printf("uart clear read buffer, cleared %d bytes hint=%s\n",cnt,hint);
    }
}

void handle_uart_parameter_change(){
    if(!uart_advance) return;
    if(!uart_port) return ;
    int baud=nc2k_states.BSR & 0x0f;
    if(baud>12) baud=12;
    int translated_baudrate;
    switch (baud){
        case 0: translated_baudrate= 230400; break;
        case 1: translated_baudrate= 115200; break;
        case 2: translated_baudrate= 57600; break;
        case 3: translated_baudrate= 38400; break;
        case 4: translated_baudrate= 19200; break;
        case 5: translated_baudrate= 9600; break;
        case 6: translated_baudrate= 4800; break;
        case 7: translated_baudrate= 2400; break;
        case 8: translated_baudrate= 1200; break;
        case 9: translated_baudrate= 600; break;
        case 10: translated_baudrate= 300; break;
        case 11: translated_baudrate= 150; break;
        case 12: translated_baudrate= 75; break;
        default:
            assert(false);
    }
    if(translated_baudrate != current_baudrate){
        if(uart_log_level>=1) printf("changing baudrate to %d\n",translated_baudrate);
        check(sp_set_baudrate(uart_port, translated_baudrate));
        current_baudrate=translated_baudrate;
    }
    int wordlen= (nc2k_states.LCR &0x01) ?8:7;
    if(wordlen != current_wordlen){
        if(uart_log_level>=1) printf("changing wordlen to %d\n", wordlen);
        check(sp_set_bits(uart_port, wordlen));
        current_wordlen=wordlen;
    }
    int stopbits= (nc2k_states.LCR &0x02) ?2:1;
    if(stopbits != current_stopbits){
        if(uart_log_level>=1) printf("changing stopbits to %d\n", stopbits);
        check(sp_set_stopbits(uart_port, stopbits));
        current_stopbits=stopbits;
    }
    enum sp_parity parity = SP_PARITY_NONE;
    int paritybits= (nc2k_states.LCR >>2) &0x7;
    if(paritybits == 0) parity=SP_PARITY_NONE;
    else if(paritybits == 1) parity=SP_PARITY_ODD;
    else if(paritybits == 3) parity=SP_PARITY_EVEN;
    else if(paritybits == 5) parity=SP_PARITY_MARK;
    else if(paritybits == 7) parity=SP_PARITY_SPACE;
    else {
        if(uart_log_level>=1) printf("unsupported parity setting %d, set to none\n",paritybits);
        parity=SP_PARITY_NONE;
    }
    if(parity != current_parity){
        if(uart_log_level>=1) printf("changing parity to %d\n", parity);
        check(sp_set_parity(uart_port, parity));
        current_parity=parity;
    }
}

/* 红外那条路也走这个串口（nc3000 的 IRCR 分流把 UART 交给红外时用得上） */
bool serial_port_is_open(){ return uart_port != nullptr; }
void serial_port_write_byte(uint8_t byte){
    if(!uart_port) return;
    unsigned int timeout_ms = 1000;
    check(sp_blocking_write(uart_port, &byte, 1, timeout_ms));
}
void serial_port_poll_incoming(void){
    if(!irda_uart_active() || !uart_port) return;
    for(int i = 0; i < 64; i++){
        int waiting = sp_input_waiting(uart_port);
        if(waiting <= 0) break;
        unsigned char buf[2];
        unsigned int timeout_ms = 1000;
        int r = check(sp_blocking_read(uart_port, buf, 1, timeout_ms));
        if(r != 1) break;
        irda_inject(buf[0]);
    }
}
#else
void open_serial_port(char *port_name){
    printf("WARN: open_serial_port() is called but it is disabled at compile time\n");
}
bool is_write_ready() {if(uart_goes_to_061()) return true; if(NC3_LINK()) return true; return false;}
bool is_read_ready() {if(uart_goes_to_061()) return nc3_dsp_rx_ready(); if(NC3_LINK()) return irda_rx_ready(); return false;}
void write_one_byte(uint8_t byte) {if(uart_tx_byte_to_061(byte)) { nc3_dsp_write(byte); return; } if(NC3_LINK()) { irda_send(byte); } return;}
uint8_t read_one_byte() {if(uart_goes_to_061()) return nc3_dsp_read(); if(NC3_LINK()) return irda_recv(); return 0xff;}
void clear_read_buffer(const char *hint){return;}
void handle_uart_parameter_change(){}
bool serial_port_is_open(){ return false; }
void serial_port_write_byte(uint8_t byte){ (void)byte; }
void serial_port_poll_incoming(void){}
#endif
/*
====================
uart wqx io handle
====================
*/

uint8_t &RHR=nc2k_states.RHR, &THR=nc2k_states.THR;
uint8_t &BSR=nc2k_states.BSR;
uint8_t &CSTOP=nc2k_states.CSTOP;
uint8_t &GPC=nc2k_states.GPC;

uint8_t read_3a_inner(){
    if(bk==0){
        /*if( (TMR&0x20) == 0 and (IVR&0x04) ==0){
            printf("uart read but both not enabled\n");
            return 0xff ;
        }*/
        if((nc2k_states.TMR&0x20) == 0 ){
            if(uart_log_level>=1) printf("uart read but uart not enabled\n");
            return 0xff;
        }
        /*if((IVR&0x04) == 0 ){
            printf("uart read but clock not enabled\n");
            return 0xff;
        }*/
        return read_one_byte();
    } else if(bk==1){
        return BSR;
    } else if(bk==2){
        return CSTOP;
    } else if(bk==3){
        return GPC;
    }
    assert(false);
}
uint8_t read_3a(){
    uint8_t ret= read_3a_inner();
    if(uart_log_level>=2){
        printf("read_3a(), returned %02x\n",ret);
    }
    return ret;
}
void write_3a(uint8_t value){
    if(uart_log_level>=2){
        printf("write_3a(), value %02x\n",value);
    }
    if(bk==0){
        /*if( (TMR&0x20) == 0 and (IVR&0x04) ==0){
            printf("uart write but both not enabled\n");
            return ;
        }*/
        if((nc2k_states.TMR&0x20) == 0 ){
            if(uart_log_level>=1) printf("uart write but uart not enabled\n");
            return ;
        }
        /*if((IVR&0x04) == 0 ){
            printf("uart write but clock not enabled\n");
            return ;
        }*/
        write_one_byte(value);
    }else if(bk==1){
        BSR=value;
        handle_uart_parameter_change();
        BSR&=0xcf;// 4 5 are zero when read back
        if(uart_log_level>=1){
            printf("write BSR=%02x baudrate=%x\n",value,value &0xf);
        }
    }else if(bk==2){
        CSTOP=value;
    } else if(bk==3){
        GPC=value;
        //extern uint8_t P05;
        //P05&=~GPC;
    }else assert(false);
}

uint8_t &LSR=nc2k_states.LSR, &LCR=nc2k_states.LCR;
uint8_t &IRCR=nc2k_states.IRCR;
uint8_t &CSTART=nc2k_states.CSTART;
uint8_t &RESERVED=nc2k_states.RESERVED;

uint8_t read_3b_inner(){
    if(bk==0){//LSR
        //lda LSReg
        //and #10011110b
        //bne wait_empty_err

        uint8_t ret=0;
        if(is_write_ready()) ret|=0x60;
        if(nc2k_states.TMR&0x20 /*&& IVR&0x04*/){ //uart enabled
            if(is_read_ready()) ret|=0x01;
        }
        return ret;
    }else if(bk==1){
        return IRCR;
    }else if(bk==2){
        return CSTART;
    }else if(bk==3){
        return RESERVED;
    }
    assert(false);
}
uint8_t read_3b(){
    uint8_t ret= read_3b_inner();
    if(uart_log_level>=2){
        printf("read_3b(), returned %02x\n",ret);
    }
    return ret;
}
void write_3b(uint8_t value){
    if(uart_log_level>=2){
        printf("write_3b(), value %02x\n",value);
    }
    if(bk==0){
        LCR=value;
        handle_uart_parameter_change();
    }else if(bk==1){
        //irda control register
        if(uart_log_level>=1 && IRCR!=value){
            printf("[irda] IRCR %02X -> %02X (%s)\n", IRCR, value,
                   value? "UART 归红外（IrDA）" : "UART 归 061");
        }
        IRCR=value;
    }else if(bk==2){
        CSTART=value;
    }else if(bk==3){
        RESERVED=value;
    }else{
        assert(false);
    }
    //ram_io[0x3b]=value;
}


uint8_t &MCR=nc2k_states.MCR;
uint8_t &MSR=nc2k_states.MSR;
uint8_t &TMR=nc2k_states.TMR;
uint8_t &P05=nc2k_states.P05;

uint8_t read_3c_inner(){
    if(bk==0){
        if(MCR == 0x02){
            //lda     #00000010b
            //sta     MCReg           ;clear
            // handle "clear"?
        }
        MCR&=0xef;
        if(TMR&0x20 /*&& IVR&0x04*/){ //uart enabled
            if(is_read_ready()){
                MCR|=0x10;
            }
        }
        return MCR;
    } else if(bk==1){
        return MSR;
    } else if(bk==2){
        return TMR;
    } else if(bk==3){
        //output value are read back
        //how to handle input value?
        return P05;
    }
    assert(false);
}
uint8_t read_3c(){
    uint8_t ret= read_3c_inner();
    if(uart_log_level>=2){
        printf("read_3c(), returned %02x\n",ret);
    }
    return ret;
}
void write_3c(uint8_t value){
    if(uart_log_level>=2){
        printf("write_3c(), value %02x\n",value);
    }
    if(bk==0){
        MCR=value;
    } else if(bk==1){
        MSR=value;
    } else if(bk==2){
        if((TMR^value)&0x20){
            if(uart_log_level>=1) printf("uart enable/disable change\n");
            clear_read_buffer("uart enable/disable change");
        }
        TMR=value;
        if(value &0x20){
            //UART enable
        }else{
            clear_read_buffer("uart disable");
            //UART disable
        }
    } else if(bk==3){
        //save output value for read back
        P05=value&GPC;
    }
    else assert(false);
}

uint8_t &IVR=nc2k_states.IVR; //only for UCE bit
uint8_t &FCR=nc2k_states.FCR;
uint8_t &IER=nc2k_states.IER;
//uint8_t BK_ONLY;
uint8_t read_3d_inner(){
    if(bk==0) {
        return get_iv()<<3|(IVR&0x04)|bk;
    }
    else if(bk==1){
        return FCR|bk;
    }
    else if(bk==2){
        return IER|bk;
    }
    else if(bk==3){
        return bk;
    }
    assert(false);
}
void write_3d(uint8_t value){
    if(uart_log_level>=2){
        printf("write_3d(), value %02x\n",value);
    }
    uint8_t new_bk=value &3;
    value&=0xfc;
    if(bk==0){
        if((IVR^value)&0x4){
            if(uart_log_level>=1) printf("uart clock change\n");
            //clear_read_buffer("uart clock change");
        }
        
        IVR=value &0x4;//UCE: enable UART clock
        if(value&0x4){
            //enable uart clock
        }else{
            //clear_read_buffer("uart clock disable");
            //disable uart clock
        }

        //TODO how to handle IV write?
    }
    else if(bk==1){
        if(value & 0x10){
            if(uart_log_level>=1) printf("RFRST\n");
            //clear_read_buffer("RFRST");
            //TODO RFRST
        }
        if(value & 0x20){
            if(uart_log_level>=1) printf("TFRST\n");
            //TODO TFRST
        }
        if(value & 0x80){
            if(uart_log_level>=1) printf("BKRT\n");
        }
        if(value & 0xc0){
            if(uart_log_level>=1) printf("FIFO trigger not zero !!! value= %x\n",value>>6);
        }
        // 3 is zero when read back
        // 45 should be zero when read back as well??
        FCR=value&0xc8;
    }
    else if(bk==2){
        if(value!=0){
            if(uart_log_level>=1) printf("IER not zero!!! value= %2x\n",value);
        }
        IER=value;
    }
    else if(bk==3){
        //no action
    }
    else{
        assert(false);
    }
    bk=new_bk;
}

uint8_t read_3d(){
    uint8_t ret= read_3d_inner();
    if(uart_log_level>=2){
        printf("read_3d(), returned %02x\n",ret);
    }
    return ret;
}
