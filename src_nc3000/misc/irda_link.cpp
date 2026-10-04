#include "comm.h"
#include "misc/irda_link.h"
#include "state.h"
#include "iv_uart.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

#if defined(__MINGW32__)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET irda_sock_t;
  #define IRDA_SOCK_BAD INVALID_SOCKET
  static int irda_sock_err(){ return WSAGetLastError(); }
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  typedef int irda_sock_t;
  #define IRDA_SOCK_BAD (-1)
  static int irda_sock_err(){ return errno; }
#endif

extern nc2k_states_t nc2k_states;

static irda_sock_t irda_sock = IRDA_SOCK_BAD;
static struct sockaddr_in irda_peer;
static bool irda_peer_ok = false;
static bool irda_echo = false;
static uint64_t irda_last_poll_cycles = 0;

int irda_link_enabled(){ return irda_sock != IRDA_SOCK_BAD; }

void irda_link_init(void){
    static bool done = false;
    if(done) return;
    done = true;

    const char *bind_s = getenv("NC3_IR_UDP");
    if(!bind_s || atoi(bind_s) <= 0) return;         /* 没配 = 不启用 */
    int bind_port = atoi(bind_s);

    char peer_host[64] = "127.0.0.1";
    int  peer_port = bind_port + 1;                  /* 默认：两个实例用相邻端口 */
    const char *peer_s = getenv("NC3_IR_PEER");
    if(peer_s && *peer_s){
        const char *colon = strrchr(peer_s, ':');
        if(colon){
            size_t n = (size_t)(colon - peer_s);
            if(n >= sizeof(peer_host)) n = sizeof(peer_host) - 1;
            memcpy(peer_host, peer_s, n);
            peer_host[n] = 0;
            peer_port = atoi(colon + 1);
        }else{
            peer_port = atoi(peer_s);
        }
    }
    const char *echo_s = getenv("NC3_IR_ECHO");
    irda_echo = (echo_s && atoi(echo_s)) ? true : false;

#if defined(__MINGW32__)
    WSADATA wsa;
    if(WSAStartup(MAKEWORD(2,2), &wsa) != 0){
        printf("[irda] WSAStartup failed\n");
        return;
    }
#endif
    irda_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(irda_sock == IRDA_SOCK_BAD){
        printf("[irda] socket() failed (%d)\n", irda_sock_err());
        return;
    }
    struct sockaddr_in me;
    memset(&me, 0, sizeof(me));
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    me.sin_port = htons((unsigned short)bind_port);
    if(bind(irda_sock, (struct sockaddr *)&me, sizeof(me)) != 0){
        printf("[irda] bind 127.0.0.1:%d failed (%d)\n", bind_port, irda_sock_err());
#if defined(__MINGW32__)
        closesocket(irda_sock);
#else
        close(irda_sock);
#endif
        irda_sock = IRDA_SOCK_BAD;
        return;
    }
    /* 非阻塞：poll 不能把模拟器卡住 */
    unsigned long nb = 1;
#if defined(__MINGW32__)
    ioctlsocket(irda_sock, FIONBIO, &nb);
#else
    int fl = fcntl(irda_sock, F_GETFL, 0);
    fcntl(irda_sock, F_SETFL, fl | O_NONBLOCK);
#endif

    memset(&irda_peer, 0, sizeof(irda_peer));
    irda_peer.sin_family = AF_INET;
    irda_peer.sin_port = htons((unsigned short)peer_port);
    irda_peer.sin_addr.s_addr = inet_addr(peer_host);
    irda_peer_ok = (irda_peer.sin_addr.s_addr != INADDR_NONE);

    printf("[irda] bridge: bind 127.0.0.1:%d -> peer %s:%d%s\n",
           bind_port, peer_host, peer_port, irda_echo ? " (echo)" : "");
}

void irda_link_poll(void){
    irda_link_init();                       /* 没配 NC3_IR_UDP 的话这里就直接返回 */
    if(irda_sock == IRDA_SOCK_BAD) return;
    /* 限频：每 ~1 模拟毫秒收一次，够快又不会在固件的紧密循环里狂调 recv */
    uint64_t cyc = nc2k_states.cycles;
    uint32_t per = CYCLES_SECOND / 1000;
    if(per == 0) per = 1;
    if(cyc - irda_last_poll_cycles < per) return;
    irda_last_poll_cycles = cyc;

    for(int i = 0; i < 8; i++){                 /* 一次最多捞 8 个包 */
        unsigned char buf[512];
        struct sockaddr_in from;
#if defined(__MINGW32__)
        int fromlen = sizeof(from);
#else
        socklen_t fromlen = sizeof(from);
#endif
        int n = recvfrom(irda_sock, (char *)buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &fromlen);
        if(n <= 0) break;
        for(int k = 0; k < n; k++) irda_inject(buf[k]);
        if(uart_log_level >= 1) printf("[irda] udp rx %d byte(s)\n", n);
        if(irda_echo){
            sendto(irda_sock, (const char *)buf, n, 0,
                   (struct sockaddr *)&from, fromlen);
        }
    }
}

void irda_link_send(const unsigned char *data, int len){
    if(irda_sock == IRDA_SOCK_BAD || !irda_peer_ok || len <= 0) return;
    sendto(irda_sock, (const char *)data, len, 0,
           (struct sockaddr *)&irda_peer, sizeof(irda_peer));
}
