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
u8 * RxBdSpacePtr;
u8 * TxBdSpacePtr;


#define RXBD_CNT       32	/* Number of RxBDs to use */
#define TXBD_CNT       32	/* Number of TxBDs to use */

volatile s32 FramesRx;		/* Frames have been received */
volatile s32 FramesTx;		/* Frames have been sent */
volatile s32 DeviceErrors;	/* Number of errors */

char srcMac[] = { 0x00, 0x18, 0x3E, 0x03, 0x61, 0x7D};
char srcIp[] = {192, 168, 1, 10};

char hostIp[] = {192, 168, 1, 103};

char boardcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* Function Prototype */
LONG sendArpRequest(char * dstIp, XEmacPs ** macPtr);
LONG ethHdr(EthernetFrame * FramePtr, char * dstMac, char * srcMac, u8 type);
LONG ethFrameArp(EthernetFrame * FramePtr, char * DstIp);

typedef struct {
    uint16_t htype;        // Hardware Type
    uint16_t ptype;        // Protocol Type
    uint8_t  hlen;         // Hardware Address Length
    uint8_t  plen;         // Protocol Address Length
    uint16_t oper;         // Operation (ARP request/reply)
    uint8_t  sha[6];       // Sender Hardware Address (MAC)
    uint8_t  spa[4];       // Sender Protocol Address (IP)
    uint8_t  tha[6];       // Target Hardware Address (MAC)
    uint8_t  tpa[4];       // Target Protocol Address (IP)
} __attribute__((packed)) ArpPkt;

uint16_t htons(uint16_t hostshort) {
    // Check the endianess of the system
    uint16_t test = 0x0102;
    uint8_t *bytePtr = (uint8_t *)&test;

    if (bytePtr[0] == 0x01) {
        // The system is big-endian, so no conversion is needed
        return hostshort;
    } else {
        // The system is little-endian, so swap the bytes
        return (hostshort >> 8) | (hostshort << 8);
    }
}

/****************************************************************************/
/**
* This function places an ARP packet into the receiving Frame
*
* @param    Destination IP 4 Bytes
* @param    Pointer to the MAC instance pointer
*
* @return   Success or Failure.
*
* @note     None.
*
*****************************************************************************/
LONG sendArpRequest(char * dstIp, XEmacPs ** macInstPtr) {
    LONG Status = 0;
    u32 TxFrameLength;
    XEmacPs * macPtr = *macInstPtr;
    XEmacPs_Bd * Bd1Ptr;
    FramesTx = 0;

    Status = ethHdr(&TxFrame, boardcastMac, srcMac, ETH_IP4_TYPE);
    Status |= ethFrameArp(&TxFrame, dstIp);

	TxFrameLength = 60;

    //Flush the frame from cache
    if (macPtr->Config.IsCacheCoherent == 0) {
		Xil_DCacheFlushRange((UINTPTR)&TxFrame, sizeof(EthernetFrame));
	}
    
    /*
	 * Allocate, setup, and enqueue 1 TxBDs. The first BD will
	 * describe the first 32 bytes of TxFrame and the rest of BDs
	 * will describe the rest of the frame.
	 *
	 * The function below will allocate 1 adjacent BDs with Bd1Ptr
	 * being set as the lead BD.
	 */
	Status = XEmacPs_BdRingAlloc(&(XEmacPs_GetTxRing(macPtr)),
				      1, &Bd1Ptr);
	if (Status != XST_SUCCESS) {
		xil_printf("Error allocating TxBD\n\r");
		return XST_FAILURE;
	}

    XEmacPs_BdSetAddressTx(Bd1Ptr, (UINTPTR)&TxFrame);
	XEmacPs_BdSetLength(Bd1Ptr, TxFrameLength);
	XEmacPs_BdClearTxUsed(Bd1Ptr);
	XEmacPs_BdSetLast(Bd1Ptr);

	xil_printf("Finished initializing TxFrame\n\r");

    /*
	 * Enqueue to HW
	 */
	Status = XEmacPs_BdRingToHw(&(XEmacPs_GetTxRing(macPtr)),
				     1, Bd1Ptr);
	if (Status != XST_SUCCESS) {
		xil_printf("Error committing TxBD to HW\n\r");
		return XST_FAILURE;
	}
	if (macPtr->Config.IsCacheCoherent == 0) {
		Xil_DCacheFlushRange((UINTPTR)Bd1Ptr, 64);
	}

    xil_printf("Queuing Block Descriptor\n\r");

    XEmacPs_SetQueuePtr(macPtr, macPtr->TxBdRing.BaseBdAddr, 0, XEMACPS_SEND);

    xil_printf("Starting MAC\n\r");

    XEmacPs_Start(macPtr);

    xil_printf("Transmitting Frame\n\r");
    
    XEmacPs_Transmit(macPtr);

    while (!FramesTx);

    if (XEmacPs_BdRingFromHwTx(&(XEmacPs_GetTxRing(macPtr)),
				    1, &Bd1Ptr) == 0) {
    	xil_printf
			("TxBDs were not ready for post processing\n\r");
		return XST_FAILURE;
	}

    Status = XEmacPs_BdRingFree(&(XEmacPs_GetTxRing(macPtr)),
				     1, Bd1Ptr);

	if (Status != XST_SUCCESS) {
		xil_printf("Error freeing up TxBDs\n\r");
		return XST_FAILURE;
	}
    return Status;
}

/****************************************************************************/
/**
* This function places an ARP packet into the receiving Frame
*
* @param    FramePtr is a pointer to the frame to change.
* @param    Destination MAC 6 Bytes
* @param    Source MAC 6 Bytes
* @param    Type of Ethernet Frame 2 Bytes
*
* @return   Success or Failure.
*
* @note     None.
*
*****************************************************************************/
LONG ethHdr(EthernetFrame * FramePtr, char * dstMac, char * srcMac, u8 type) {
    LONG Status = XST_SUCCESS;

    if (FramePtr == NULL) {
        return XST_FAILURE;
    }

    char * ptr = (char *) FramePtr;

    /* Set the destination MAC address */
    memcpy((void *) ptr, (void *) dstMac, 6);
    ptr += 6;

    /* Set the source MAC address */
    memcpy((void *) ptr, (void *) srcMac, 6);
    ptr += 6;

    /* Set the Ethernet type */
    *ptr = type;
    return Status;
}

/****************************************************************************/
/**
* This function places an ARP packet into the receiving Frame
*
* @param    FramePtr is a pointer to the frame to change.
* @param    Destination Ip
*
* @return   Sucess or Failure.
*
* @note     None.
*
*****************************************************************************/
LONG ethFrameArp(EthernetFrame * FramePtr, char * DstIp) {
    LONG Status = XST_SUCCESS;

	u8 *arpPtr = (u8 *)FramePtr + 14; // Dst MAC(6) + Src MAC(6) + Type(2)
	ArpPkt * Ptr = (ArpPkt *) arpPtr;
	Ptr->htype = htons(0x0001); //Ethernet
	Ptr->ptype = htons(0x0800); //IPv4
	Ptr->hlen = 0x06; //hardware Address length
	Ptr->plen = 0x04; //IP protocol address length
	Ptr->oper = htons(0x0001); //ARP is protocol 1, Reply is 2
	memcpy(Ptr->sha, srcMac, sizeof(Ptr->sha));
	memcpy(Ptr->spa, srcIp, sizeof(Ptr->spa));
	memset(Ptr->tha, 0, sizeof(Ptr->tha));//Doesn't matter, just fill with 6 bytes. Requesting Destionation MAC
	memcpy(Ptr->tpa, DstIp, sizeof(Ptr->tpa));

    return Status;
}


