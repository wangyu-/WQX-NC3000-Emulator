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
    if(irda_loopback_enabled()) irda_rx_q.push_back(byte);
}
/* 给将来的主机桥（串口/UDP/另一台模拟器）用：往接收队列塞一个字节 */
void irda_inject(uint8_t byte){
    irda_rx_q.push_back(byte);
}
bool irda_rx_ready(){
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
#if defined (ENABLE_SERIAL_PORT)
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
    if(irda_uart_active()) return true;
    if(NC3_LINK()) return true;
    if(!uart_port) return false;
    int waiting = check(sp_output_waiting(uart_port));
    if (waiting < 0) {
        assert(false);
    }
    return (waiting == 0); 
}

bool is_read_ready() {
    if(irda_uart_active()) return irda_rx_ready();
    if(NC3_LINK()) return nc3_dsp_rx_ready();
    if(!uart_port) return false;
    int bytes_waiting = check(sp_input_waiting(uart_port));
    if (bytes_waiting < 0) {
        assert(false);
    }
    return bytes_waiting>0;
}
void write_one_byte(uint8_t byte) {
    if(irda_uart_active()) { irda_send(byte); return; }
    if(NC3_LINK()) { nc3_dsp_write(byte); return; }
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
    if(irda_uart_active()) return irda_recv();
    if(NC3_LINK()) return nc3_dsp_read();
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
#else
void open_serial_port(char *port_name){
    printf("WARN: open_serial_port() is called but it is disabled at compile time\n");
}
bool is_write_ready() {if(irda_uart_active()) return true; if(NC3_LINK()) return true; return false;}
bool is_read_ready() {if(irda_uart_active()) return irda_rx_ready(); if(NC3_LINK()) return nc3_dsp_rx_ready(); return false;}
void write_one_byte(uint8_t byte) {if(irda_uart_active()) { irda_send(byte); return; } if(NC3_LINK()) { nc3_dsp_write(byte); } return;}
uint8_t read_one_byte() {if(irda_uart_active()) return irda_recv(); if(NC3_LINK()) return nc3_dsp_read(); return 0xff;}
void clear_read_buffer(const char *hint){return;}
void handle_uart_parameter_change(){}
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
