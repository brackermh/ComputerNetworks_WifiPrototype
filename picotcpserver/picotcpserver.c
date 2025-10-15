#include <stdio.h>
#include "pico/stdlib.h"        //pico stdlib
#include "pico/cyw43_arch.h"    //pico wifi drivers
#include "lwip/ip4_addr.h"
#include "lwip/tcp.h"           //pico tcp 
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip_addr.h"
#include "lwip/inet.h"

// access point config
#define WIFI_SSID "PicoTCPServer"
#define WIFI_PASSWORD "headerfile"
#define SERVER_PORT 4849

struct netif netif;

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

void start_tcp_server() {
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        printf("Failed to create PCB\n");
        return;
    }

    err_t err = tcp_bind(pcb, IP_ADDR_ANY, SERVER_PORT);
    if (err != ERR_OK) {
        printf("TCP bind failed: %d\n", err);
        return;
    }

    pcb = tcp_listen(pcb);
    tcp_accept(pcb, tcp_accept_cb);
    printf("TCP server listening on port %d\n", SERVER_PORT);
}

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("Client connected\n");
    tcp_recv(newpcb, tcp_recv_cb);
    return ERR_OK;
}

static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        printf("Connection closed\n");
        tcp_close(tpcb);
        return ERR_OK;
    }

    printf("Received %d bytes: %.*s\n", p->len, p->len, (char *)p->payload);

    // echo back data received from client
    tcp_write(tpcb, p->payload, p->len, TCP_WRITE_FLAG_COPY);
    pbuf_free(p);
    return ERR_OK;
}

int main() {
    stdio_init_all();       //init pico 

    if (cyw43_arch_init()) {        //init wifi driver
        printf("Wi-Fi init failed\n");
        return -1;
    }

    printf("Starting SoftAP: %s\n", WIFI_SSID);     

    //enable wifi access point
    cyw43_arch_enable_ap_mode(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK);

    //AP start delay
    sleep_ms(2000);

    start_tcp_server();

    //forever loop polling wifi connection
    while (true) {
        cyw43_arch_poll();
        sleep_ms(100);
    }

    return 0;
}
