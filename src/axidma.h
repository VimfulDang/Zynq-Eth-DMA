#include "xparameters.h"
#include "xaxidma.h"
#include "xdebug.h"
#include "xaxidma_bdring.h"

#define DMA_DEV_ID		XPAR_AXIDMA_0_DEVICE_ID

// --- Interrupt Handling ---
// AXI DMA Interrupt IDs (Check xparameters.h for your specific hardware)
#define DMA_MM2S_INTR_ID XPAR_FABRIC_AXI_DMA_0_MM2S_INTROUT_INTR // TX Channel
#define DMA_S2MM_INTR_ID XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR // RX Channel

#define MEM_BASE_ADDR		(XPAR_PS7_DDR_0_S_AXI_BASEADDR + 0x1000000) //+16 MB

#define TX_BD_SPACE_BASE	(MEM_BASE_ADDR)
#define TX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00000FFF) //+4kB
#define RX_BD_SPACE_BASE	(MEM_BASE_ADDR + 0x00001000) 
#define RX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00001FFF) //+4kB
#define TX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00020000) //+128 kB
#define TX_BUFFER_HIGH		(MEM_BASE_ADDR + 0x0002FFFF)
#define RX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00030000) 
#define RX_BUFFER_HIGH		(MEM_BASE_ADDR + 0x0003FFFF) //+128 kB

#define NUMBER_OF_PACKETS	0x10
#define MAX_PKT_LEN         0x3FFFFF

//Set the maximum number of packets to keep track
#define NUM_PACKET_MAX      64

#define TEST_START_VALUE    0x3

#define XAXIDMA_BD_NDESC_OFFSET		0x00  /**< Next descriptor pointer */
#define XAXIDMA_BD_NDESC_MSB_OFFSET	0x04  /**< Next descriptor pointer */
#define XAXIDMA_BD_BUFA_OFFSET		0x08  /**< Buffer address */
#define XAXIDMA_BD_BUFA_MSB_OFFSET	0x0C  /**< Buffer address */
#define XAXIDMA_BD_MCCTL_OFFSET		0x10  /**< Multichannel Control Fields */
#define XAXIDMA_BD_STRIDE_VSIZE_OFFSET	0x14  /**< 2D Transfer Sizes */
#define XAXIDMA_BD_CTRL_LEN_OFFSET	0x18  /**< Control/buffer length */
#define XAXIDMA_BD_STS_OFFSET		0x1C  /**< Status */

#define XAXIDMA_BD_USR0_OFFSET		0x20  /**< User IP specific word0 */
#define XAXIDMA_BD_USR1_OFFSET		0x24  /**< User IP specific word1 */
#define XAXIDMA_BD_USR2_OFFSET		0x28  /**< User IP specific word2 */
#define XAXIDMA_BD_USR3_OFFSET		0x2C  /**< User IP specific word3 */
#define XAXIDMA_BD_USR4_OFFSET		0x30  /**< User IP specific word4 */

typedef struct {
    u32 nxtDesc;
    u32 nxtDescMsb;
    u32 bufAddr;
    u32 bufAddrMsb;
    u32 reserved[2];
    u32 ctrl;
    u32 status;
    u32 app[4];
} __attribute__((packed)) axi_bd;

typedef struct {
    u32 * start_addr;
    u32 * end_addr;
} __attribute__((packed)) packet_inf_t;

//Packet Count will increment with every EOF seen by RX interrupt
struct rx_pkt_trk_t {
    packet_inf_t packet_inf[NUM_PACKET_MAX];
    packet_inf_t * wr_ptr;
    packet_inf_t * rd_ptr;
} rx_pkt_trk;

int RxSetup(XAxiDma * AxiDmaInstPtr);
int TxSetup(XAxiDma * AxiDmaInstPtr);
int SendPackets(XAxiDma * AxiDmaInstPtr);

// Forward declarations for ISRs and setup
void TxIntrHandler(void *Callback);
void RxIntrHandler(void *Callback);
int SetupAxiDmaInterrupts(INTC * IntcInstancePtr, XAxiDma * AxiDmaPtr);
void DisableAxiDmaInterrupts(INTC * IntcInstancePtr);
int AxiDma_BringBdFromHw(XAxiDma_BdRing * ringPtr);
void XAxiDma_RingSeekAhead(XAxiDma_BdRing *RingPtr, XAxiDma_Bd **BdPtr, u32 NumBd);

// Inline equivalent for XAxiDma_BdRingIntAck
void AxiDmaIntAck(XAxiDma_BdRing * RingPtr, u32 Mask)
{
	XAxiDma_WriteReg(RingPtr->ChanBase, XAXIDMA_SR_OFFSET, Mask);
}

#define XAxiDma_BdRingNext(RingPtr, BdPtr)			\
		(((UINTPTR)(BdPtr) >= (RingPtr)->LastBdAddr) ?	\
			(UINTPTR)(RingPtr)->FirstBdAddr :	\
			(UINTPTR)((UINTPTR)(BdPtr) + (RingPtr)->Separation))

/******************************************************************************
 * Move the BdPtr argument ahead an arbitrary number of BDs wrapping around
 * to the beginning of the ring if needed.
 *
 * We know if a wraparound should occur if the new BdPtr is greater than
 * the high address in the ring OR if the new BdPtr crosses the 0xFFFFFFFF
 * to 0 boundary.
 *
 * @param	RingPtr is the ring BdPtr appears in
 * @param	BdPtr on input is the starting BD position and on output is the
 *		final BD position
 * @param	NumBd is the number of BD spaces to increment
 *
 * @returns	None
 *
 * @note	This function can be used only when DMA is in SG mode
 *
 *****************************************************************************/
void XAxiDma_RingSeekAhead(XAxiDma_BdRing *RingPtr, XAxiDma_Bd **BdPtr, u32 NumBd)
{
    UINTPTR Addr = (UINTPTR)(void *)(*BdPtr);

    Addr += (RingPtr->Separation * NumBd);
    if ((Addr > RingPtr->LastBdAddr) || ((UINTPTR)(*BdPtr) > Addr))
    {
        Addr -= RingPtr->Length;
    }

    *BdPtr = (XAxiDma_Bd *)(void *)Addr;
}


u32 *Packet = (u32 *) TX_BUFFER_BASE;
XAxiDma_Bd *LastRxBdPtr = NULL;

/*
 * Device instance definitions
 */
XAxiDma AxiDma;
XAxiDma_Config *AxiDmaConfig;

LONG axiDmaInit() {
    LONG Status = 0;

    AxiDmaConfig = XAxiDma_LookupConfig(DMA_DEV_ID);

    Status = XAxiDma_CfgInitialize(&AxiDma, AxiDmaConfig);
    if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

    if(!XAxiDma_HasSg(&AxiDma)) {
		xil_printf("Device configured as simple mode \r\n");
		return XST_FAILURE;
	}

    Status = TxSetup(&AxiDma);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

    Status = RxSetup(&AxiDma);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

    return Status;
}

/*
    1. Check if ownership bit of current HwHead.
    2. If RX and completed
        2a. Check if SOF. If so, append Bd address 
*/

int AxiDma_BringBdFromHw(XAxiDma_BdRing * ringPtr) {
    int bdCount = 0;
    axi_bd * CurBdPtr = (axi_bd *) ringPtr->HwHead;
    XAXIDMA_CACHE_INVALIDATE(CurBdPtr);
    packet_inf_t * rx_pkt_ptr = rx_pkt_trk.wr_ptr;
    u32 rxStsPtr = NULL;
    u32 txStsPtr = NULL;
    u32 rxCtrlPtr = NULL;
    u32 txCtrlPtr = NULL;

    if (ringPtr->IsRxChannel) {
        rxStsPtr = CurBdPtr->status;
        rxCtrlPtr = CurBdPtr->ctrl;
        xil_printf("RX Status: 0x%08x, RX Control: 0x%08x\n\r", *(u32 *)&CurBdPtr->status, *(u32 *)&CurBdPtr->ctrl);
        while (rxStsPtr & XAXIDMA_BD_STS_COMPLETE_MASK) {
            CurBdPtr->status &= ~XAXIDMA_BD_STS_COMPLETE_MASK; // Clear the 'complete' bit using bitmasking
            bdCount += 1;
            if (rxStsPtr & XAXIDMA_BD_STS_RXSOF_MASK) {
                rx_pkt_ptr->start_addr = CurBdPtr;
            }
            if (rxStsPtr & XAXIDMA_BD_STS_RXEOF_MASK) {
                rx_pkt_ptr->end_addr = CurBdPtr;
                rx_pkt_trk.wr_ptr += 1;
                rx_pkt_ptr = rx_pkt_trk.wr_ptr;
                break; // Exit the loop upon finding EOF
            }
            CurBdPtr = (axi_bd *)((void *)XAxiDma_BdRingNext(ringPtr, CurBdPtr));
            rxStsPtr = CurBdPtr->status;
            rxCtrlPtr = CurBdPtr->ctrl;
        }
    }
    else {
        txStsPtr = CurBdPtr->status;
        txCtrlPtr = CurBdPtr->ctrl;
        xil_printf("TX Status: 0x%08x, TX Control: 0x%08x\n\r", *(u32 *)&CurBdPtr->status, *(u32 *)&CurBdPtr->ctrl);
        while (txStsPtr & XAXIDMA_BD_STS_COMPLETE_MASK) {
            CurBdPtr->status &= ~XAXIDMA_BD_STS_COMPLETE_MASK; // Clear the 'complete' bit using bitmasking
            bdCount +=1;
            CurBdPtr = (axi_bd *)((void *)XAxiDma_BdRingNext(ringPtr, CurBdPtr));
            txStsPtr = CurBdPtr->status;
            txCtrlPtr = CurBdPtr->ctrl;
        }
    }
    xil_printf("Freed %d\n", bdCount);
    ringPtr->FreeCnt += bdCount;
    ringPtr->PostCnt -= bdCount;
    XAxiDma_RingSeekAhead(ringPtr, ringPtr->PostHead, bdCount);
    return bdCount;
}

// --- AXI DMA Interrupt Setup Function ---
int SetupAxiDmaInterrupts(INTC * IntcInstancePtr, XAxiDma * AxiDmaPtr)
{
    int Status;
    XAxiDma_BdRing *TxRingPtr = XAxiDma_GetTxRing(AxiDmaPtr);
    XAxiDma_BdRing *RxRingPtr = XAxiDma_GetRxRing(AxiDmaPtr);

    // --- Connect TX Handler ---
    Status = XScuGic_Connect(IntcInstancePtr, DMA_MM2S_INTR_ID,
                             (Xil_InterruptHandler) TxIntrHandler,
                             AxiDmaPtr);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to connect TX intr handler (ID: %d)\n\r", DMA_MM2S_INTR_ID);
        return XST_FAILURE;
    }
    XScuGic_Enable(IntcInstancePtr, DMA_MM2S_INTR_ID);
    // Clear any pending TX interrupts
	AxiDmaIntAck(TxRingPtr, XAXIDMA_IRQ_ALL_MASK);
    // Enable TX interrupts in DMA core (already done in TxSetup now)
    // XAxiDma_BdRingIntEnable(TxRingPtr, XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK);


    // --- Connect RX Handler ---
    Status = XScuGic_Connect(IntcInstancePtr, DMA_S2MM_INTR_ID,
                             (Xil_InterruptHandler) RxIntrHandler,
                             AxiDmaPtr);
    if (Status != XST_SUCCESS) {
        xil_printf("Failed to connect RX intr handler (ID: %d)\n\r", DMA_S2MM_INTR_ID);
        return XST_FAILURE;
    }

    //Setup RX Packet Tracker Struct
    rx_pkt_trk.rd_ptr = rx_pkt_trk.packet_inf;
    rx_pkt_trk.wr_ptr = rx_pkt_trk.packet_inf;

    XScuGic_Enable(IntcInstancePtr, DMA_S2MM_INTR_ID);
    // Clear any pending RX interrupts
	AxiDmaIntAck(RxRingPtr, XAXIDMA_IRQ_ALL_MASK);
    // Enable RX interrupts in DMA core (already done in RxSetup now)
    // XAxiDma_BdRingIntEnable(RxRingPtr, XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK);

    // Note: Global interrupt enable (Xil_ExceptionEnable) was called in EmacPsSetupIntrSystem

    return XST_SUCCESS;
}

// --- AXI DMA Interrupt Disconnect Function ---
void DisableAxiDmaInterrupts(INTC * IntcInstancePtr)
{
    XScuGic_Disable(IntcInstancePtr, DMA_MM2S_INTR_ID);
    XScuGic_Disconnect(IntcInstancePtr, DMA_MM2S_INTR_ID);

    XScuGic_Disable(IntcInstancePtr, DMA_S2MM_INTR_ID);
    XScuGic_Disconnect(IntcInstancePtr, DMA_S2MM_INTR_ID);
}


// --- TX Interrupt Service Routine ---
void TxIntrHandler(void *Callback)
{
	u32 IrqStatus;
	int TimeOut;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;
	XAxiDma_BdRing *TxRingPtr = XAxiDma_GetTxRing(AxiDmaInst);
    u32 Error = 0;
    u32 TxDone = 0;

	/* Read pending interrupts */
	IrqStatus = XAxiDma_BdRingGetIrq(TxRingPtr);
    xil_printf("TX Interrupt Status: 0x%08x\r\n", IrqStatus);
	/* Acknowledge pending interrupts */
	AxiDmaIntAck(TxRingPtr, IrqStatus);

	/* If no interrupt is asserted, we do not do anything */
	if (!(IrqStatus & (XAXIDMA_IRQ_ERROR_MASK | XAXIDMA_IRQ_IOC_MASK))) {
		return;
	}

	/* Check for Errors */
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {
		xil_printf("TX Error Interrupt (Status: 0x%08x)\r\n", IrqStatus);
		Error = 1;
		// Potentially reset the DMA channel here
		// Consider stopping and restarting the channel
		return; // Stop processing on error
	}

	/* Check for TX completion */
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {
		int BdCount;
		XAxiDma_Bd *BdPtr;
		int Status;

		/* Get all processed BDs from hardware */
		BdCount = AxiDma_BringBdFromHw(TxRingPtr);
		if (BdCount > 0) {
            TxDone = 1;
		}
	}
}

// --- RX Interrupt Service Routine ---
void RxIntrHandler(void *Callback)
{
	u32 IrqStatus;
	int BdCount;
	XAxiDma_Bd *BdPtr;
	XAxiDma_Bd *BdCurPtr;
	int Status;
	int i;
	XAxiDma *AxiDmaInst = (XAxiDma *)Callback;
	XAxiDma_BdRing *RxRingPtr = XAxiDma_GetRxRing(AxiDmaInst);


	/* Read pending interrupts */
	IrqStatus = XAxiDma_BdRingGetIrq(RxRingPtr);
    xil_printf("RX Interrupt Status: 0x%08x\r\n", IrqStatus);

	/* Acknowledge pending interrupts */
	AxiDmaIntAck(RxRingPtr, IrqStatus);

	/* If no interrupt is asserted, we do not do anything */
	if (!(IrqStatus & (XAXIDMA_IRQ_ERROR_MASK | XAXIDMA_IRQ_IOC_MASK))) {
		return;
	}

	/* Check for Errors */
	if ((IrqStatus & XAXIDMA_IRQ_ERROR_MASK)) {
		xil_printf("RX Error Interrupt (Status: 0x%08x)\r\n", IrqStatus);

        // It might be useful to halt further processing upon RX error
        // Consider stopping and restarting the channel
		return;
	}

	/* Check for RX completion */
	if ((IrqStatus & XAXIDMA_IRQ_IOC_MASK)) {
		BdCount = AxiDma_BringBdFromHw(RxRingPtr);

		// if (BdCount > 0) {
		// 	BdCurPtr = BdPtr;
		// 	for (i = 0; i < BdCount; i++) {
		// 		u32 RxLen = XAxiDma_BdGetLength(BdCurPtr, RxRingPtr->MaxTransferLen);
		// 		UINTPTR RxBufAddr = XAxiDma_BdGetBufAddr(BdCurPtr);

		// 		xil_printf("Received Packet: BD=0x%x, Addr=0x%x, Len=%d\n\r",
		// 		           (unsigned int)BdCurPtr, (unsigned int)RxBufAddr, RxLen);

		// 		/* Invalidate the cache range */
		// 		Xil_DCacheInvalidateRange(RxBufAddr, RxLen);

		// 		// --- Process the received packet data here ---
		// 		// Example: Check first few bytes
		// 		// u8 *RxPacketData = (u8 *)RxBufAddr;
		// 		// xil_printf("  Data: %02x %02x %02x %02x ...\n\r",
		// 		//            RxPacketData[0], RxPacketData[1], RxPacketData[2], RxPacketData[3]);
		// 		// --- End Packet Processing ---

		// 		/* Get the next BD */
		// 		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
		// 	}

        //     // Signal completion as soon as packets are processed
        //     RxDone = 1;

        //     /* Free the processed BDs */
        //     Status = XAxiDma_BdRingFree(RxRingPtr, BdCount, BdPtr);
        //     if (Status != XST_SUCCESS) {
        //         xil_printf("Error freeing RX BDs! Status: %d\r\n", Status);
        //         Error = 1;
        //     }

        //     // IMPORTANT: Re-submit buffers to the hardware to receive more packets.
        //     // This requires allocating free BDs and assigning buffers again.
        //     // We need to ensure the buffers freed above are available.
        //     // Let's try re-allocating the same number we just processed.
        //     XAxiDma_Bd *NewBdPtr;
        //     int FreeBdCount = XAxiDma_BdRingGetFreeCnt(RxRingPtr);
        //     if (FreeBdCount >= BdCount) {
        //         Status = XAxiDma_BdRingAlloc(RxRingPtr, BdCount, &NewBdPtr);
        //         if (Status != XST_SUCCESS) {
        //             xil_printf("Error allocating new RX BDs! Status: %d\r\n", Status);
        //             Error = 1;
        //         } else {
        //             XAxiDma_Bd *AllocBdCurPtr = NewBdPtr;
        //             BdCurPtr = BdPtr; // Use the original list to get buffer addresses
        //             for (i = 0; i < BdCount; i++) {
        //                 UINTPTR RxBufAddr = XAxiDma_BdGetId(BdCurPtr); // Get original buffer address via ID
        //                 u32 BufferLenPerBd = RxRingPtr->MaxTransferLen; // Or re-calculate if needed

        //                 Status = XAxiDma_BdSetBufAddr(AllocBdCurPtr, RxBufAddr);
        //                 if (Status != XST_SUCCESS) { Error = 1; break; }

        //                 Status = XAxiDma_BdSetLength(AllocBdCurPtr, BufferLenPerBd, RxRingPtr->MaxTransferLen);
        //                  if (Status != XST_SUCCESS) { Error = 1; break; }

        //                 XAxiDma_BdSetCtrl(AllocBdCurPtr, 0);
        //                 XAxiDma_BdSetId(AllocBdCurPtr, RxBufAddr);

        //                 AllocBdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, AllocBdCurPtr);
        //                 BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
        //             }

        //             if (!Error) {
        //                 Status = XAxiDma_BdRingToHw(RxRingPtr, BdCount, NewBdPtr);
        //                 if (Status != XST_SUCCESS) {
        //                     xil_printf("Error submitting new RX BDs to HW! Status: %d\r\n", Status);
        //                     Error = 1;
        //                 }
        //             }
        //         }
        //     } else {
        //          xil_printf("Warning: Not enough free RX BDs (%d) to re-submit %d\n\r", FreeBdCount, BdCount);
        //          // Handle this case - maybe only re-submit available count?
        //     }

            // RxDone is now set earlier
    }
}

/*****************************************************************************/
/**
*
* This function sets up the TX channel of a DMA engine to be ready for packet
* transmission
*
* @param	AxiDmaInstPtr is the instance pointer to the DMA engine.
*
* @return	- XST_SUCCESS if the setup is successful
*		- XST_FAILURE if setup is failure
*
* @note     None.
*
******************************************************************************/
int TxSetup(XAxiDma * AxiDmaInstPtr)
{
	XAxiDma_BdRing *TxRingPtr;
	XAxiDma_Bd BdTemplate;
	int Delay = 0;
	int Coalesce = 1;
	int Status;
	u32 BdCount;

	TxRingPtr = XAxiDma_GetTxRing(&AxiDma);

	/* Disable all TX interrupts initially, they will be enabled later */
	// XAxiDma_BdRingIntDisable(TxRingPtr, XAXIDMA_IRQ_ALL_MASK); // Keep disabled for now, enable after setup

	/* Set TX delay and coalesce */
	XAxiDma_BdRingSetCoalesce(TxRingPtr, Coalesce, Delay);

	/* Setup Tx BD space  */
	BdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT,
				TX_BD_SPACE_HIGH - TX_BD_SPACE_BASE + 1);

	Status = XAxiDma_BdRingCreate(TxRingPtr, TX_BD_SPACE_BASE,
				TX_BD_SPACE_BASE,
				XAXIDMA_BD_MINIMUM_ALIGNMENT, BdCount);
	if (Status != XST_SUCCESS) {
		xil_printf("failed create BD ring in txsetup\r\n");

		return XST_FAILURE;
	}

	/*
	 * We create an all-zero BD as the template.
	 */
	XAxiDma_BdClear(&BdTemplate);

	Status = XAxiDma_BdRingClone(TxRingPtr, &BdTemplate);
	if (Status != XST_SUCCESS) {
		xil_printf("failed bdring clone in txsetup %d\r\n", Status);

		return XST_FAILURE;
	}

	/* Enable TX Interrupts (IOC and Error) */
	XAxiDma_BdRingIntEnable(TxRingPtr, XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK);

	/* Start the TX channel */
	Status = XAxiDma_BdRingStart(TxRingPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("failed start bdring txsetup %d\r\n", Status);
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function sets up RX channel of the DMA engine to be ready for packet
* reception
*
* @param	AxiDmaInstPtr is the pointer to the instance of the DMA engine.
*
* @return	- XST_SUCCESS if the setup is successful
*		- XST_FAILURE if setup is failure
*
* @note		None.
*
******************************************************************************/
int RxSetup(XAxiDma * AxiDmaInstPtr)
{
	XAxiDma_BdRing *RxRingPtr;
	int Delay = 0;
	int Coalesce = 1;
	int Status;
	XAxiDma_Bd BdTemplate;
	XAxiDma_Bd *BdPtr;
	XAxiDma_Bd *BdCurPtr;
	u32 BdCount;
	u32 FreeBdCount;
	UINTPTR RxBufferPtr;
	u32 BufferLenPerBd;
	u32 TotalRxBufferSize;
	int i;

	RxRingPtr = XAxiDma_GetRxRing(&AxiDma);

	/* Disable all RX interrupts initially, they will be enabled later */
	// XAxiDma_BdRingIntDisable(RxRingPtr, XAXIDMA_IRQ_ALL_MASK); // Keep disabled for now, enable after setup

	/* Set delay and coalescing */
	XAxiDma_BdRingSetCoalesce(RxRingPtr, Coalesce, Delay);

	/* Setup Rx BD space */
	BdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT,
				RX_BD_SPACE_HIGH - RX_BD_SPACE_BASE + 1);

	Status = XAxiDma_BdRingCreate(RxRingPtr, RX_BD_SPACE_BASE,
				RX_BD_SPACE_BASE,
				XAXIDMA_BD_MINIMUM_ALIGNMENT, BdCount);

	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Setup an all-zero BD as the template for the Rx channel.
	 */
	XAxiDma_BdClear(&BdTemplate);

	Status = XAxiDma_BdRingClone(RxRingPtr, &BdTemplate);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Attach buffers to RxBD ring so we are ready to receive packets
	 */
	FreeBdCount = XAxiDma_BdRingGetFreeCnt(RxRingPtr);
	Status = XAxiDma_BdRingAlloc(RxRingPtr, FreeBdCount, &BdPtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	BdCurPtr = BdPtr;
	RxBufferPtr = RX_BUFFER_BASE;
	TotalRxBufferSize = RX_BUFFER_HIGH - RX_BUFFER_BASE + 1;
	BufferLenPerBd = TotalRxBufferSize / FreeBdCount; // Integer division gives bytes per BD

	xil_printf("RxSetup: TotalRxBufferSize=%u, FreeBdCount=%u, BufferLenPerBd=%u\n\r",
	           TotalRxBufferSize, FreeBdCount, BufferLenPerBd);

	for (i = 0; i < FreeBdCount; i++) {

		Status = XAxiDma_BdSetBufAddr(BdCurPtr, RxBufferPtr);
		if (Status != XST_SUCCESS) {
			xil_printf("Rx set buffer addr %x on BD %x failed %d\r\n",
			(unsigned int)RxBufferPtr, (UINTPTR)BdCurPtr,
								Status);

			return XST_FAILURE;
		}

		// Use the calculated buffer length per BD
		Status = XAxiDma_BdSetLength(BdCurPtr, BufferLenPerBd,
				RxRingPtr->MaxTransferLen);
		if (Status != XST_SUCCESS) {
			xil_printf("Rx set length %d on BD %x failed %d\r\n",
			    BufferLenPerBd, (unsigned int)BdCurPtr, Status);

			return XST_FAILURE;
		}

		/* Receive BDs do not need to set anything for the control
		 * The hardware will set the SOF/EOF bits per stream status
		 */
		XAxiDma_BdSetCtrl(BdCurPtr, 0);
		XAxiDma_BdSetId(BdCurPtr, RxBufferPtr);
		RxBufferPtr += BufferLenPerBd; // Increment by the calculated length

		// Ensure the buffer pointer doesn't exceed the allocated space,
		// especially important if TotalRxBufferSize is not perfectly divisible by FreeBdCount.
		// The last BD might get slightly less space implicitly due to integer division,
		// or slightly more if we adjusted the last BD's length, but this increment keeps pointers aligned.
		if (RxBufferPtr > RX_BUFFER_HIGH + 1) {
             xil_printf("ERROR: RxBufferPtr (%x) exceeded RX_BUFFER_HIGH (%x)\r\n", (unsigned int)RxBufferPtr, RX_BUFFER_HIGH);
             // Handle error appropriately, maybe return XST_FAILURE
             // For now, just cap it to avoid overflow in calculation, though the BD setup might be wrong.
             RxBufferPtr = RX_BUFFER_HIGH + 1;
        }


		if (i == (FreeBdCount - 1)) {
			LastRxBdPtr = BdCurPtr;
			// Optional: Adjust the length of the last BD to use remaining space if division wasn't exact.
			// u32 LastBdLen = BufferLenPerBd + (TotalRxBufferSize % FreeBdCount);
			// XAxiDma_BdSetLength(BdCurPtr, LastBdLen, RxRingPtr->MaxTransferLen);
		}

		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
	}

	Status = XAxiDma_BdRingToHw(RxRingPtr, FreeBdCount, BdPtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Enable RX Interrupts (IOC and Error) */
	XAxiDma_BdRingIntEnable(RxRingPtr, XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK);

	/* Start RX DMA channel */
	Status = XAxiDma_BdRingStart(RxRingPtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

int SendPackets(XAxiDma * AxiDmaInstPtr) {
    XAxiDma_BdRing *TxRingPtr;
    u8 *TxPacket;
    u8 Value;
    XAxiDma_Bd *BdPtr;
    int Status;
    int i;
    UINTPTR BufAddr;
    XAxiDma_Bd *CurBdPtr;
    u32 BufferLenPerTxBd;
    u32 TotalTxBufferSize;
    u32 TotalTxBds;
    u32 ActualDataLen;

    TxRingPtr = XAxiDma_GetTxRing(AxiDmaInstPtr);

    // Calculate buffer length per TX BD
    TotalTxBufferSize = TX_BUFFER_HIGH - TX_BUFFER_BASE + 1;
    TotalTxBds = TxRingPtr->AllCnt; // Get total configured BDs
    if (TotalTxBds == 0) {
        xil_printf("ERROR: No TX BDs configured!\r\n");
        return XST_FAILURE;
    }
    BufferLenPerTxBd = TotalTxBufferSize / TotalTxBds;

    xil_printf("SendPackets: TotalTxBufferSize=%u, TotalTxBds=%u, BufferLenPerTxBd=%u\n\r",
               TotalTxBufferSize, TotalTxBds, BufferLenPerTxBd);

    // Ensure requested packets don't exceed available BDs
    if (NUMBER_OF_PACKETS > TotalTxBds) {
        xil_printf("ERROR: NUMBER_OF_PACKETS (%u) exceeds available TX BDs (%u)\r\n",
                   NUMBER_OF_PACKETS, TotalTxBds);
        return XST_FAILURE;
    }

    // Ensure calculated length is not zero
    if (BufferLenPerTxBd == 0) {
        xil_printf("ERROR: Calculated BufferLenPerTxBd is zero. Check TX buffer size and BD count.\r\n");
        return XST_FAILURE;
    }

    /* Create pattern in the packet to transmit based on calculated length */
    TxPacket = (u8 *) Packet;

    Value = TEST_START_VALUE;
    ActualDataLen = BufferLenPerTxBd * NUMBER_OF_PACKETS; // Total data length to prepare

    // Ensure we don't try to write past the allocated TX buffer space
    if (ActualDataLen > TotalTxBufferSize) {
        xil_printf("Warning: Calculated ActualDataLen (%u) > TotalTxBufferSize (%u). Clamping.\r\n", ActualDataLen, TotalTxBufferSize);
        ActualDataLen = TotalTxBufferSize;
    }

    xil_printf("Preparing %u bytes of data for TX...\n\r", ActualDataLen);
    for(i = 0; i < ActualDataLen; i ++) {
        TxPacket[i] = Value;
        Value = (Value + 1) & 0xFF;
    }

    /* Flush the specific buffer range used for transmission */
    Xil_DCacheFlushRange((UINTPTR)TxPacket, ActualDataLen);

    /* Allocate BDs */
    Status = XAxiDma_BdRingAlloc(TxRingPtr, NUMBER_OF_PACKETS, &BdPtr);
    if (Status != XST_SUCCESS) {
        return XST_FAILURE;
    }

    /* Set up the BDs using the information of the packet to transmit */
    BufAddr = (UINTPTR)Packet;
    CurBdPtr = BdPtr;

    for (i = 0; i < NUMBER_OF_PACKETS; i++) {
        u32 CrBits = 0;

        Status = XAxiDma_BdSetBufAddr(CurBdPtr, BufAddr);
        if (Status != XST_SUCCESS) {
            xil_printf("Tx set buffer addr %x on BD %x failed %d\r\n",
            (unsigned int)BufAddr, (UINTPTR)CurBdPtr, Status);

            return XST_FAILURE;
        }

        // Use the calculated buffer length per BD
        Status = XAxiDma_BdSetLength(CurBdPtr, BufferLenPerTxBd,
                TxRingPtr->MaxTransferLen);
        if (Status != XST_SUCCESS) {
            xil_printf("Tx set length %d on BD %x failed %d\r\n",
                BufferLenPerTxBd, (UINTPTR)CurBdPtr, Status);

            return XST_FAILURE;
        }

        /* Set the SOF bit for the first BD */
        if (i == 0) {
            CrBits |= XAXIDMA_BD_CTRL_TXSOF_MASK;
        }
        /* Set the EOF bit for the last BD */
        if (i == (NUMBER_OF_PACKETS - 1)) {
            CrBits |= XAXIDMA_BD_CTRL_TXEOF_MASK;
        }
        /* Set the control bits for the current BD */
        XAxiDma_BdSetCtrl(CurBdPtr, CrBits);

        XAxiDma_BdSetId(CurBdPtr, BufAddr);

        BufAddr += BufferLenPerTxBd; // Increment by calculated length
        CurBdPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(TxRingPtr, CurBdPtr);
    }

    /* Enable TX Interrupts (IOC and Error) */
    XAxiDma_BdRingIntEnable(TxRingPtr, XAXIDMA_IRQ_IOC_MASK | XAXIDMA_IRQ_ERROR_MASK);

    /* Give the BD to DMA to kick off the transmission. */
    Status = XAxiDma_BdRingToHw(TxRingPtr, NUMBER_OF_PACKETS, BdPtr);
    if (Status != XST_SUCCESS) {
        xil_printf("to hw failed %d\r\n", Status);

        return XST_FAILURE;
    }

    return XST_SUCCESS;
}
