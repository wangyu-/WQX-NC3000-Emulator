#pragma once
/*
 * 红外（IrDA）主机桥：把模拟器的 IR 收发对接到 UDP，让
 *   - 两个模拟器实例互相做"红外通讯"（A 的 IR → B 的 IR，反之亦然）
 *   - 或者用一个 UDP 小工具/脚本当对端（收发都是裸字节，一个字节一个 datagram 也行）
 *
 * 环境变量（缺 NC3_IR_UDP 就完全不启用，行为跟没桥一样）：
 *   NC3_IR_UDP=<本地端口>        绑定 127.0.0.1:<本地端口> 收 IR 字节
 *   NC3_IR_PEER=<host:port>      发到哪（默认 127.0.0.1:<本地端口+1>）
 *   NC3_IR_ECHO=1                收到什么就原样回给发送方（当"回声对端"用）
 *
 * 例：两个实例，A 收 7001 发 7002，B 收 7002 发 7001：
 *   A: set NC3_IR_UDP=7001 & set NC3_IR_PEER=127.0.0.1:7002
 *   B: set NC3_IR_UDP=7002 & set NC3_IR_PEER=127.0.0.1:7001
 *
 * 说明：IR 字节是"裸字节流"，不做任何成帧/校验 —— 固件那侧自己带协议。
 */

void irda_link_init(void);
void irda_link_poll(void);                      /* 收 socket（内部限频，随便调） */
void irda_link_send(const unsigned char *data, int len);
int  irda_link_enabled(void);
