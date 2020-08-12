/******************************************************************************
*
* Copyright (C) 2009 - 2014 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* Use of the Software is limited solely to applications:
* (a) running on a Xilinx device, or
* (b) that interact with a Xilinx device through a bus or interconnect.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
*
******************************************************************************/

#include <stdio.h>

#include "xparameters.h"

#include "netif/xadapter.h"

#include "platform.h"
#include "platform_config.h"
#if defined (__arm__) || defined(__aarch64__)
#include "xil_printf.h"
#endif

#include "lwip/tcp.h"
#include "lwipopts.h"
#include "xil_cache.h"
#define MAX_FLASH_LEN 4*1024*1024 //4MB
/* defined by each RAW mode application */
#define true 1
#define false 0

void print_app_header();
int start_tcp_application();
int transfer_data();
void tcp_fasttmr(void);
void tcp_slowtmr(void);
void lwip_init();
int send_data( int length , char * Send_Buffer );
err_t accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err);
err_t recv_callback(void *arg, struct tcp_pcb *tpcb,struct pbuf *p, err_t err);
int send_data_v2( int length , char * Send_Buffer );

/*user defined*/
char FlashRxBuffer[MAX_FLASH_LEN] ;
int recevied_flag = 0 ;
static struct tcp_pcb *tcp_pcb_connected = NULL;

/*this is flag for transfer*/
static int echo_back = 0 ;
static int start_update = 0 ;
static int ReceivedCount = 0 ;
//int send_data(struct pbuf* p);
/* missing declaration in lwIP */


extern volatile int TcpFastTmrFlag;
extern volatile int TcpSlowTmrFlag;
static struct netif server_netif;
struct netif *echo_netif;

void
print_ip(char *msg, struct ip_addr *ip) 
{
	print(msg);
	xil_printf("%d.%d.%d.%d\n\r", ip4_addr1(ip), ip4_addr2(ip), 
			ip4_addr3(ip), ip4_addr4(ip));
}

void
print_ip_settings(struct ip_addr *ip, struct ip_addr *mask, struct ip_addr *gw)
{

	print_ip("Board IP: ", ip);
	print_ip("Netmask : ", mask);
	print_ip("Gateway : ", gw);
}

int main()
{
	struct ip_addr ipaddr, netmask, gw;

	/* the mac address of the board. this should be unique per board */
	unsigned char mac_ethernet_address[] =
	{ 0x00, 0x0a, 0x35, 0x00, 0x01, 0x02 };

	echo_netif = &server_netif;
	init_platform();

	/* initliaze IP addresses to be used */
	IP4_ADDR(&ipaddr,  192, 168,  1, 10);
	IP4_ADDR(&netmask, 255, 255, 255,  0);
	IP4_ADDR(&gw,      192, 168,  1,  1);


	//print_app_header();

	lwip_init();

  	/* Add network interface to the netif_list, and set it as default */
	if (!xemac_add(echo_netif, &ipaddr, &netmask,
						&gw, mac_ethernet_address,
						PLATFORM_EMAC_BASEADDR)) {
		xil_printf("Error adding N/W interface\n\r");
		return -1;
	}
	netif_set_default(echo_netif);

	/* now enable interrupts */
	platform_enable_interrupts();

	/* specify that the network if is up */
	netif_set_up(echo_netif);
	print_ip_settings(&ipaddr, &netmask, &gw);

	/* start the application (web server, rxtest, txtest, etc..) */
	start_tcp_application();

	/* receive and process packets */
	while (1) {
		if (TcpFastTmrFlag) {
			tcp_fasttmr();
			TcpFastTmrFlag = 0;
		}
		if (TcpSlowTmrFlag) {
			tcp_slowtmr();
			TcpSlowTmrFlag = 0;
		}
		xemacif_input(echo_netif);
		if (start_update == 1){
			start_update = 0 ;
			ReceivedCount = 0 ;
			/*
			int Status;
			Status = update_qspi(&QspiInstance, QSPI_DEVICE_ID, ReceivedCount, FlashRxBuffer) ;
			if (Status != XST_SUCCESS)
				xil_printf("Write Flash Error!\r\n") ;
			else{
				StartUpdate = 0 ;
				ReceivedCount = 0;
			}
			*/
		}else if(echo_back == 1){
			echo_back = 0 ;
			//send_data(ReceivedCount, &FlashRxBuffer[0]);
			send_data_v2(ReceivedCount, &FlashRxBuffer[0]);
			ReceivedCount = 0 ;
			/*
			if (ReceivedCount > 16384){
				send_data(16384, &FlashRxBuffer[send_again*16384]);
				send_again = send_again + 1 ;
				echo_back = 1 ;
			}else{
				send_data(ReceivedCount, &FlashRxBuffer[0]);
			}
			*/
		}else{
			//waiting for recevied data!
		}
	}

  
	/* never reached */
	cleanup_platform();

	return 0;
}


//there are must be some re-call back function
//to indicate that the data have been recived and to
//make the recieved flag to be true


//here we will open a tcp block and
//after then xemacif_input()get the data
//then turn on the reci_flga to indicate
//that the data have been recived
//this is a server app
int start_tcp_application(){
	err_t status;
	unsigned port = 8080;
	tcp_pcb_connected = tcp_new();
	
	if (!tcp_pcb_connected) {
			xil_printf("Error creating PCB. Out of Memory\n\r");
			return -1;
	}

	/* bind to specified @port */
	status = tcp_bind(tcp_pcb_connected, IP_ADDR_ANY, port);
	if (status != ERR_OK) {
		xil_printf("Unable to bind to port %d: err = %d\n\r", port, status);
		return -2;
	}

	/* we do not need any arguments to callback functions */
	tcp_arg(tcp_pcb_connected, NULL);

	/* listen for connections */
	tcp_pcb_connected = tcp_listen(tcp_pcb_connected);
	if (!tcp_pcb_connected) {
		xil_printf("Out of memory while tcp_listen\n\r");
		return -3;
	}

	/* specify callback to use for incoming connections */
	tcp_accept(tcp_pcb_connected, accept_callback);

	
	xil_printf("TCP server app started @ port %d\n\r", port);

	return 0;

}

err_t accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	static int connection = 1;

	/* set the receive callback for this connection */
	tcp_recv(newpcb, recv_callback);

	/* just use an integer number indicating the connection id as the
	   callback argument */
	tcp_arg(newpcb, (void*)(UINTPTR)connection);

	/* increment for subsequent accepted connections */
	connection++;

	return ERR_OK;
}

err_t recv_callback(void *arg, struct tcp_pcb *tpcb, \
		struct pbuf *p, err_t err){
	//recevied data pointer
	char *pData;
	/* do not read the packet if we are not in ESTABLISHED state */
	if (!p) {
		tcp_close(tpcb);
		tcp_recv(tpcb, NULL);
		return ERR_OK;
	}
	tcp_recved(tpcb, p->len);
	pData = (char *) p->payload;
	int tcp_len = p->len ;

	if ( tcp_len == 9 && !(memcmp("echo_back", p->payload, 9)) ){
		echo_back = 1 ;
	}else if ( tcp_len == 12 && !(memcmp("start_update", p->payload, 12)) ){
		start_update = 1 ;
	}else{
		memcpy(&FlashRxBuffer[ReceivedCount], pData, tcp_len);
		ReceivedCount += tcp_len ;
		xil_printf("ReceivedCount bytes is %d \r\n", ReceivedCount);
	}
	
	tcp_pcb_connected = tpcb ;
	pbuf_free(p);
	return ERR_OK;
}

int send_data( int length , char * Send_Buffer ){
	err_t err;

	struct tcp_pcb * send_pcb;
	send_pcb = tcp_pcb_connected ;
	if (!send_pcb)
		return -1;

	//struct pbuf* p_head , p, q ;
	//q = pbuf_alloc(PBUF_TRANSPORT, 1024, PBUF_POOL);
	struct pbuf*  p;
	int send_count = length / 1024 + 1 ;
	for (int i = 0 ; i < send_count; ++i){
		if(i == send_count-1){
			p = pbuf_alloc(PBUF_TRANSPORT, length - i*1024, PBUF_POOL);
			memcpy(p->payload, &Send_Buffer[i*1024], length - i*1024);
			xil_printf("avaiable_snd_buf size is : %d\r\n", tcp_sndbuf(send_pcb) );
			err = tcp_write(send_pcb, p->payload, length - i*1024, TCP_WRITE_FLAG_COPY);
			if (err != ERR_OK) {
				xil_printf("txperf: Error on tcp_write: %d\r\n", err);
				send_pcb = NULL;
				return -1;
			}
		}else{
			p = pbuf_alloc(PBUF_TRANSPORT, 1024, PBUF_POOL);
			memcpy(p->payload, &Send_Buffer[i*1024], 1024);
			
			xil_printf("avaiable_snd_buf size is : %d\r\n", tcp_sndbuf(send_pcb) );

			err = tcp_write(send_pcb, p->payload, 1024, TCP_WRITE_FLAG_COPY);
			if (err != ERR_OK) {
				xil_printf("txperf: Error on tcp_write: %d\r\n", err);
				send_pcb = NULL;
				return -1;
			}
		}
		err = tcp_output(send_pcb);
		if (err != ERR_OK) {
			xil_printf("txperf: Error on tcp_output: %d\r\n",err);
			return -1;
		}
		pbuf_free(p);
	}
	xil_printf("func into send_data: \r\n");
	return -1;
	
	/*
	p = pbuf_alloc(PBUF_TRANSPORT, length, PBUF_POOL);
	if (!p) {
		xil_printf("pbuf_alloc %d fail\n\r", length);
		return -3;
	}
	memcpy(p->payload, Send_Buffer, length);
	//err = tcp_write(tpcb_send, p->payload, p->len, 3);
	//*
	if (tcp_sndbuf(tpcb) > p->len) {
		xil_printf("error in tcp_sndbuf: \r\n");
		return -1;
		}
	 

	//err = tcp_write(tpcb, Send_Buffer, length, TCP_WRITE_FLAG_COPY);
	//err = tcp_write(send_pcb, p->payload, p->len, TCP_WRITE_FLAG_COPY);
	err = tcp_write(send_pcb, p->payload, length, TCP_WRITE_FLAG_COPY);
	if (err != ERR_OK) {
		xil_printf("txperf: Error on tcp_write: %d\r\n", err);
		send_pcb = NULL;
		return -1;
	}
	
	err = tcp_output(send_pcb);
	if (err != ERR_OK) {
		xil_printf("txperf: Error on tcp_output: %d\r\n",err);
		return -1;
	}
	pbuf_free(p);
	xil_printf("func into send_data: \r\n");

	return -1;
	*/
}

int send_data_v2( int length , char * Send_Buffer ){
	err_t err;

	struct tcp_pcb * send_pcb;
	send_pcb = tcp_pcb_connected ;
	if (!send_pcb)
		return -1;
	struct pbuf*  p;
	int send_count = length / 1024 + 1 ;
	for (int i = 0 ; i < send_count; ++i){
		if(i == send_count-1){
			p = pbuf_alloc(PBUF_TRANSPORT, length - i*1024, PBUF_POOL);
			memcpy(p->payload, &Send_Buffer[i*1024], length - i*1024);

			xil_printf("avaiable_snd_buf size is : %d\r\n", tcp_sndbuf(send_pcb) );
			
			_Bool HEAD_FLAG = true; // Used to breake the while loop
			while(HEAD_FLAG){
				err = tcp_write(send_pcb, p->payload, length - i*1024, TCP_WRITE_FLAG_COPY);
					if (err == ERR_OK) {
						err = tcp_output(send_pcb);
						if (err != ERR_OK) {
							xil_printf("txperf: Error on tcp_output: %d\r\n",err);
							return -1;
						}
						HEAD_FLAG = false;
					}
				xemacif_input(echo_netif);
			}
		}else{
			p = pbuf_alloc(PBUF_TRANSPORT, 1024, PBUF_POOL);
			memcpy(p->payload, &Send_Buffer[i*1024], 1024);
			
			xil_printf("avaiable_snd_buf size is : %d\r\n", tcp_sndbuf(send_pcb) );
			_Bool HEAD_FLAG = true; // Used to breake the while loop
			while(HEAD_FLAG){
				err = tcp_write(send_pcb, p->payload, 1024, TCP_WRITE_FLAG_COPY);
					if (err == ERR_OK) {
						err = tcp_output(send_pcb);
						if (err != ERR_OK) {
							xil_printf("txperf: Error on tcp_output: %d\r\n",err);
							return -1;
						}
						HEAD_FLAG = false;
					}
				xemacif_input(echo_netif);
			}
		}
		pbuf_free(p);
	}
	xil_printf("func into send_data: \r\n");
	return -1;
}
