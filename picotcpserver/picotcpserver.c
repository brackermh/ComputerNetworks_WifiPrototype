#include <stdio.h>
#include "pico/stdlib.h"        //pico SDK
#include "pico/cyw43_arch.h"    //pico wifi drivers
#include "lwip/ip4_addr.h"      //pico IP stack
#include "lwip/tcp.h"           //pico IP stack
#include "lwip/dhcp.h"          //pico IP stack
#include "lwip/init.h"          //pico IP stack
#include "lwip/netif.h"         //pico IP stack
#include "lwip/ip_addr.h"       //pico IP stack
#include "lwip/inet.h"          //pico IP stack

// access point config
#define WIFI_SSID "PicoTCPServer"
#define WIFI_PASSWORD "headerfile"
#define SERVER_PORT 4849
#define MAX_CLIENTS 2

struct tcp_pcb *clients[MAX_CLIENTS] = {NULL, NULL};

struct netif netif;

static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

void start_tcp_server() {
    //create TCP protocol control block
    struct tcp_pcb *pcb = tcp_new();

    if (!pcb) {
        printf("Failed to create PCB\n");
        return;
    }

    //bind server to listen on any local ip on port 4849
    err_t err = tcp_bind(pcb, IP_ADDR_ANY, SERVER_PORT);

    if (err != ERR_OK) {
        printf("TCP bind failed: %d\n", err);
        return;
    }

    //listen for incoming connections
    pcb = tcp_listen(pcb);
    //run accept function when client connects
    tcp_accept(pcb, tcp_accept_cb);
    printf("TCP server listening on port %d\n", SERVER_PORT);
}

//callback whenever client connects
static err_t tcp_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err) {
    printf("Client connected\n");

    // Find an empty slot
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == NULL) {
            clients[i] = newpcb;
            tcp_arg(newpcb, &clients[i]);
            tcp_recv(newpcb, tcp_recv_cb);
            tcp_err(newpcb, NULL);
            printf("Client %d connected successfully\n", i + 1);
            return ERR_OK;
        }
    }

    // too many clients
    printf("Too many clients, closing new connection\n");
    tcp_close(newpcb);
    return ERR_ABRT;
}


//handle incoming data
static err_t tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    if (!p) {
        printf("Connection closed\n");
        tcp_close(tpcb);
        return ERR_OK;
    }

    // Get the remote client's IP address
    ip_addr_t client_ip = tpcb->remote_ip;
    printf("Received %d bytes from %s: %.*s\n",
           p->len,
           ip4addr_ntoa(&client_ip),
           p->len,
           (char *)p->payload);

    // Echo data back to that same client
    tcp_write(tpcb, p->payload, p->len, TCP_WRITE_FLAG_COPY);
    pbuf_free(p);
    return ERR_OK;
}

void configure_static_ip() {
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 192,168,4,1);
    IP4_ADDR(&netmask, 255,255,255,0);
    IP4_ADDR(&gw, 192,168,4,1);
    netif_set_addr(&cyw43_state.netif[CYW43_ITF_AP], &ipaddr, &netmask, &gw);
    printf("AP static IP: %s\n", ip4addr_ntoa(&ipaddr));
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
    //define static IP
    configure_static_ip();


    //AP start delay
    sleep_ms(2000);

    start_tcp_server();

    //forever loop handling wifi background tasks and sleeps preventing cpu saturation
    while (true) {
        cyw43_arch_poll();
        sleep_ms(100);
    }

    return 0;
}
