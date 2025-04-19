#include "xparameters.h"
 #include "xaxidma.h"
 #include "xdebug.h"

#define DMA_DEV_ID		XPAR_AXIDMA_0_DEVICE_ID

#define MEM_BASE_ADDR		(XPAR_PS7_DDR_0_S_AXI_BASEADDR + 0x1000000) //+16 MB

#define TX_BD_SPACE_BASE	(MEM_BASE_ADDR)
#define TX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00000FFF) //+4kB
#define RX_BD_SPACE_BASE	(MEM_BASE_ADDR + 0x00001000) 
#define RX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00001FFF) //+4kB
#define TX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00020000) //+128 kB
#define RX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00030000) 
#define RX_BUFFER_HIGH		(MEM_BASE_ADDR + 0x0003FFFF) //+128 kB

#define NUMBER_OF_PACKETS	0x10
#define MAX_PKT_LEN         0x3FFFFF

#define TEST_START_VALUE    0x3
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

int RxSetup(XAxiDma * AxiDmaInstPtr);
int TxSetup(XAxiDma * AxiDmaInstPtr);
int SendPackets(XAxiDma * AxiDmaInstPtr);

u32 *Packet = (u32 *) TX_BUFFER_BASE;
static XAxiDma_Bd *LastRxBdPtr = NULL;

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
static int TxSetup(XAxiDma * AxiDmaInstPtr)
{
	XAxiDma_BdRing *TxRingPtr;
	XAxiDma_Bd BdTemplate;
	int Delay = 0;
	int Coalesce = 1;
	int Status;
	u32 BdCount;

	TxRingPtr = XAxiDma_GetTxRing(&AxiDma);

	/* Disable all TX interrupts before Tx BD space setup */

	XAxiDma_BdRingIntDisable(TxRingPtr, XAXIDMA_IRQ_ALL_MASK);

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
static int RxSetup(XAxiDma * AxiDmaInstPtr)
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
	int i;

	RxRingPtr = XAxiDma_GetRxRing(&AxiDma);

	/* Disable all RX interrupts before RxBD space setup */

	XAxiDma_BdRingIntDisable(RxRingPtr, XAXIDMA_IRQ_ALL_MASK);

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

	for (i = 0; i < FreeBdCount; i++) {

		Status = XAxiDma_BdSetBufAddr(BdCurPtr, RxBufferPtr);
		if (Status != XST_SUCCESS) {
			xil_printf("Rx set buffer addr %x on BD %x failed %d\r\n",
			(unsigned int)RxBufferPtr, (UINTPTR)BdCurPtr,
								Status);

			return XST_FAILURE;
		}

		Status = XAxiDma_BdSetLength(BdCurPtr, MAX_PKT_LEN,
				RxRingPtr->MaxTransferLen);
		if (Status != XST_SUCCESS) {
			xil_printf("Rx set length %d on BD %x failed %d\r\n",
			    MAX_PKT_LEN, (UINTPTR)BdCurPtr, Status);

			return XST_FAILURE;
		}

		/* Receive BDs do not need to set anything for the control
		 * The hardware will set the SOF/EOF bits per stream status
		 */
		XAxiDma_BdSetCtrl(BdPtr, 0);
		XAxiDma_BdSetId(BdCurPtr, RxBufferPtr);
		RxBufferPtr += MAX_PKT_LEN;

		if (i == (FreeBdCount - 1)) {
			LastRxBdPtr = BdCurPtr;
		}

		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
	}

	Status = XAxiDma_BdRingToHw(RxRingPtr, FreeBdCount, BdPtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

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

	/* Create pattern in the packet to transmit
	 */
	TxPacket = (u8 *) Packet;

	Value = TEST_START_VALUE;

	for(i = 0; i < MAX_PKT_LEN * NUMBER_OF_PACKETS; i ++) {
		TxPacket[i] = Value;

		Value = (Value + 1) & 0xFF;
	}

	/* Flush the buffers before the DMA transfer, in case the Data Cache
	 * is enabled
	 */
	Xil_DCacheFlushRange((UINTPTR)TxPacket, MAX_PKT_LEN *
							NUMBER_OF_PACKETS);
	Xil_DCacheFlushRange((UINTPTR)RX_BUFFER_BASE, MAX_PKT_LEN *
						 NUMBER_OF_PACKETS);

	TxRingPtr = XAxiDma_GetTxRing(AxiDmaInstPtr);

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

		Status = XAxiDma_BdSetLength(CurBdPtr, MAX_PKT_LEN,
				TxRingPtr->MaxTransferLen);
		if (Status != XST_SUCCESS) {
			xil_printf("Tx set length %d on BD %x failed %d\r\n",
			    MAX_PKT_LEN, (UINTPTR)CurBdPtr, Status);

			return XST_FAILURE;
		}

		if (i == 0) {
			CrBits |= XAXIDMA_BD_CTRL_TXSOF_MASK;

		if (i == (NUMBER_OF_PACKETS - 1)) {
			CrBits |= XAXIDMA_BD_CTRL_TXEOF_MASK;
			XAxiDma_BdSetCtrl(CurBdPtr,
					XAXIDMA_BD_CTRL_TXEOF_MASK);
		}
		XAxiDma_BdSetCtrl(CurBdPtr, CrBits);

		XAxiDma_BdSetId(CurBdPtr, BufAddr);

		BufAddr += MAX_PKT_LEN;
		CurBdPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(TxRingPtr, CurBdPtr);
	}

	/* Give the BD to DMA to kick off the transmission. */
	Status = XAxiDma_BdRingToHw(TxRingPtr, NUMBER_OF_PACKETS, BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("to hw failed %d\r\n", Status);

		return XST_FAILURE;
	}

	return XST_SUCCESS;
}
