#include <stdio.h>
#include <stdlib.h>
#include "xparameters.h"
#include "xil_printf.h"
#include "xemacps.h"
#include "xscugic.h"
#include "xil_cache.h"

#define ETH_IP4_TYPE    0x0800
#define ICMP_PROTOCOL       0x01
#define ICMP_ECHO_REQ   0x08

#define XEMACPS_HDR_SIZE        14U	/* size of Ethernet header */
#define XEMACPS_MTU             1500U	/* max MTU size of Ethernet frame */
#define XEMACPS_TRL_SIZE        4U	/* size of Ethernet trailer (FCS) */
#define XEMACPS_MAX_FRAME_SIZE       (XEMACPS_MTU + XEMACPS_HDR_SIZE + \
        XEMACPS_TRL_SIZE)

typedef char EthernetFrame[XEMACPS_MAX_FRAME_SIZE]
	__attribute__ ((aligned(64)));


EthernetFrame TxFrame;		/* Transmit buffer */
EthernetFrame RxFrame;		/* Receive buffer */

u8 bd_space[0x100000] __attribute__ ((aligned (0x100000)));
u8 * RxBdSpacePtr, TxBdSpacePtr;

char srcMac[] = { 0x00, 0x18, 0x3E, 0x03, 0x61, 0x7D};
