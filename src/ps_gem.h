#include "xparameters.h"
#include "xemacps.h"
#include "xil_exception.h"
#include "xil_mmu.h"



/*
 * SLCR setting
 */

#define SLCR_LOCK_KEY_VALUE		0x767B
#define SLCR_UNLOCK_KEY_VALUE		0xDF0D

#define SLCR_LOCK_ADDR			(XPS_SYS_CTRL_BASEADDR + 0x4)
#define SLCR_UNLOCK_ADDR		(XPS_SYS_CTRL_BASEADDR + 0x8)
#define SLCR_GEM0_CLK_CTRL_ADDR		(XPS_SYS_CTRL_BASEADDR + 0x140)

#define EMACPS_SLCR_DIV_MASK	0xFC0FC0FF

XEmacPs macInstance;
XScuGic IntcInstance;
#define INTC		XScuGic
#define EMACPS_DEVICE_ID	XPAR_XEMACPS_0_DEVICE_ID
#define INTC_DEVICE_ID		XPAR_SCUGIC_SINGLE_DEVICE_ID

/*
	PHY Registers
*/
u32 XEmacPsDetectPHY(XEmacPs * EmacPsInstancePtr);
#define EMACPS_LOOPBACK_SPEED_1G 1000	/* 1000Mbps */

#define PHY_ID_RTL			0x1C
#define PHY_DETECT_REG1 	0x2
#define PHY_DETECT_REG2 	0x3
#define PHY_REG0_RESET		0x8000
#define PHY_REG0_LOOPBACK 	0x4000
#define PHY_REG1_LINKSTS	0x0004
#define PHY_REG1_AUTONEG	0x0020

/*
    GEM Registers
*/
#define RXSR_OFFSET         0x00000020  //RX Status
#define RXSR_BUFNA_MASK     0x00000001  //Buffer Not Available as it is owned by processor
#define RXSR_FRAMERX_MASK   0x00000002 //Frame Received
#define RXSR_RXOVR_MASK     0x00000004 //Receive Overrun
#define RXSR_HRESPNOK_MASK  0x00000008 //Hresp not OK

#define ISR_OFFSET          0x00000024  //Interrupt Status
#define ISR_FRAMERX_MASK    0x00000002  //Frame Received
#define ISR_RXUSED_MASK     0x00000004  //Rx Bd used bit set
#define ISR_TXUSED_MASK     0x00000008  //Tx Bd used bit set
#define ISR_RXOVR_MASK      0x00000400  //Receive Overrun
#define ISR_HREPNOK_MASK    0x00000800  //Hresp not OK

#define IER     0x00000028  //Interrupt Enable

/*
 * 	RX DDR Buffer Define
 *
 */
#define RX_BDBUF_LEN		0x800
#define DDR_BASE			XPAR_PS7_DDR_0_S_AXI_BASEADDR
#define RXBUF_BASE          DDR_BASE + 0x100000

/*
    Function Prototypes
*/
//LONG XEmacPs_SetMacAddress(XEmacPs *InstancePtr, void *AddressPtr, u8 Index);
void XEmacPsClkSetup(XEmacPs *EmacPsInstancePtr);
static LONG EmacPsSetupIntrSystem(INTC * IntcInstancePtr,
				  XEmacPs * EmacPsInstancePtr,
				  u16 EmacPsIntrId);

static void EmacPsDisableIntrSystem(INTC * IntcInstancePtr,
				     u16 EmacPsIntrId);
static void XEmacPsSendHandler(void *Callback);
static void XEmacPsRecvHandler(void *Callback);
static void XEmacPsErrorHandler(void *Callback, u8 Direction, u32 ErrorWord);
static LONG EmacPsResetDevice(XEmacPs * EmacPsInstancePtr);


LONG phyConfig(XEmacPs * macPtr, u32 speed);
LONG phyAutoNegotiate(XEmacPs * macPtr, u32 PhyAddr);
LONG phyLinkStatus(XEmacPs * macPtr, u32 PhyAddr);

LONG macInit(XEmacPs * macInstPtr, u16 * macIntrId, XEmacPs_Config * macConfigInstPtr, INTC * IntcInstancePtr);
LONG bdInit(XEmacPs * macInstPtr);
void SetRxBuf(XEmacPs * macPtr);

/*****************************************************************************/
/**
 * Set this bit to mark the last descriptor in the receive buffer descriptor
 * list.
 *
 * @param  BdPtr is the BD pointer to operate on
 *
 * @note
 * C-style signature:
 *    void XEmacPs_BdSetRxWrap(XEmacPs_Bd* BdPtr)
 *
 *****************************************************************************/
#define XEmacPs_BdSetRxWrap(BdPtr)                                 \
    (XEmacPs_BdWrite((BdPtr), XEMACPS_BD_ADDR_OFFSET,             \
    XEmacPs_BdRead((BdPtr), XEMACPS_BD_ADDR_OFFSET) |             \
    XEMACPS_RXBUF_WRAP_MASK))


///**  @name MDC clock division
// *  currently supporting 8, 16, 32, 48, 64, 96, 128, 224.
// * @{
// */
//typedef enum { MDC_DIV_8 = 0U, MDC_DIV_16, MDC_DIV_32, MDC_DIV_48,
//	MDC_DIV_64, MDC_DIV_96, MDC_DIV_128, MDC_DIV_224
//} XEmacPs_MdcDiv;

/****************************************************************************/
/**
*
* This function sets up the clock divisors for 1000Mbps.
*
* @param	EmacPsInstancePtr is a pointer to the instance of the EmacPs
*			driver.

* @return	None.
*
* @note		None.
*
*****************************************************************************/
void XEmacPsClkSetup(XEmacPs *EmacPsInstancePtr)
{
	u32 ClkCntrl;
	u32 BaseAddress = EmacPsInstancePtr->Config.BaseAddress;

	/* SLCR unlock */
	*(volatile unsigned int *)(SLCR_UNLOCK_ADDR) = SLCR_UNLOCK_KEY_VALUE;
	/* GEM0 1G clock configuration*/
	ClkCntrl =
	*(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR);
	ClkCntrl &= EMACPS_SLCR_DIV_MASK;
	ClkCntrl |= (EmacPsInstancePtr->Config.S1GDiv1 << 20);
	ClkCntrl |= (EmacPsInstancePtr->Config.S1GDiv0 << 8);
	*(volatile unsigned int *)(SLCR_GEM0_CLK_CTRL_ADDR) =
							ClkCntrl;

	/* SLCR Lock */
	*(volatile unsigned int *)(SLCR_LOCK_ADDR) = SLCR_LOCK_KEY_VALUE;
    sleep(1);

}

/****************************************************************************/
/**
*
* This function sets up each RX BD to a buffer address of Ethernet Frame Length
*
* @param	Pointer to MAC Structure to get RingPtr
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
void SetRxBuf(XEmacPs * macPtr)
{
	XEmacPs_BdRing * ringPtr = &(macPtr->RxBdRing);
	XEmacPs_Bd * bdPtr = (XEmacPs_Bd *) ringPtr->BaseBdAddr;


	for(u32 i = 0; i < ringPtr->Length; i++) {
		//Set buffer address to DDR
		XEmacPs_BdSetAddressRx(bdPtr, &RxFrame[i]);
        
		//Set Ownership to zero
        XEmacPs_BdClearRxNew(bdPtr);
        // xil_printf("Buffer Address: 0x%08X\n\r", XEmacPs_BdGetBufAddr(bdPtr) & XEMACPS_RXBUF_ADD_MASK);
		bdPtr++;
	}

	//Points to last Bd
	bdPtr = (XEmacPs_Bd *) ringPtr->HighBdAddr;
	XEmacPs_BdSetRxWrap(bdPtr);
}

/****************************************************************************/
/**
*
* This function disables the interrupts that occur for EmacPs.
*
* @param	IntcInstancePtr is the pointer to the instance of the ScuGic
*		driver.
* @param	EmacPsIntrId is interrupt ID and is typically
*		XPAR_<EMACPS_instance>_INTR value from xparameters.h.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
static void EmacPsDisableIntrSystem(INTC * IntcInstancePtr,
				     u16 EmacPsIntrId)
{
	/*
	 * Disconnect and disable the interrupt for the EmacPs device
	 */
#ifdef XPAR_INTC_0_DEVICE_ID
	XIntc_Disconnect(IntcInstancePtr, EmacPsIntrId);
#else
	XScuGic_Disconnect(IntcInstancePtr, EmacPsIntrId);
#endif

}

/****************************************************************************/
/**
*
* This the Transmit handler callback function and will increment a shared
* counter that can be shared by the main thread of operation.
*
* @param	Callback is the pointer to the instance of the EmacPs device.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
static void XEmacPsSendHandler(void *Callback)
{
	XEmacPs *EmacPsInstancePtr = (XEmacPs *) Callback;

	/*
	 * Disable the transmit related interrupts
	 */
//	xil_printf("Intr: 0x%x\n\r", *((volatile u32 *) EmacPsInstancePtr->Config.BaseAddress + 0x24));
	XEmacPs_IntDisable(EmacPsInstancePtr, (XEMACPS_IXR_TXCOMPL_MASK |
		XEMACPS_IXR_TX_ERR_MASK));
	/*
	 * Increment the counter so that main thread knows something
	 * happened.
	 */
	FramesTx++;
//	xil_printf("TX Frame: %u\n\r", FramesTx);
}


/****************************************************************************/
/**
*
* This is the Receive handler callback function and will increment a shared
* counter that can be shared by the main thread of operation.
*
* @param	Callback is a pointer to the instance of the EmacPs device.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
static void XEmacPsRecvHandler(void *Callback)
{
	XEmacPs *macPtr = (XEmacPs *) Callback;

	/*
	 * Disable the transmit related interrupts
	 */
	XEmacPs_IntDisable(macPtr, (XEMACPS_IXR_FRAMERX_MASK |
		XEMACPS_IXR_RX_ERR_MASK));
	/*
	 * Increment the counter so that main thread knows something
	 * happened.
	 */
	FramesRx++;

    //Read RX Status Register
    u32 RxStatus = XEmacPs_ReadReg(macPtr->Config.BaseAddress, RXSR_OFFSET);
    //Clear out reserved bit[31:4] and Rx Frame bit
    if (RxStatus & ~(RXSR_FRAMERX_MASK | 0xFFFFFFF0)) {
        FramesRxErr++;
    }

    //Clear out the RX Status Register
    XEmacPs_WriteReg(macPtr->Config.BaseAddress, RXSR_OFFSET, 0xF);

    //Clear out the ISR
    XEmacPs_WriteReg(macPtr->Config.BaseAddress, ISR_OFFSET, \
        (ISR_FRAMERX_MASK | ISR_RXUSED_MASK | ISR_RXOVR_MASK | ISR_HREPNOK_MASK));

    //Re-enable the interrupts
    XEmacPs_IntEnable(macPtr, (XEMACPS_IXR_FRAMERX_MASK | XEMACPS_IXR_RX_ERR_MASK));

}


/****************************************************************************/
/**
*
* This is the Error handler callback function and this function increments
* the error counter so that the main thread knows the number of errors.
*
* @param	Callback is the callback function for the driver. This
*		parameter is not used in this example.
* @param	Direction is passed in from the driver specifying which
*		direction error has occurred.
* @param	ErrorWord is the status register value passed in.
*
* @return	None.
*
* @note		None.
*
*****************************************************************************/
static void XEmacPsErrorHandler(void *Callback, u8 Direction, u32 ErrorWord)
{
	XEmacPs *EmacPsInstancePtr = (XEmacPs *) Callback;

	/*
	 * Increment the counter so that main thread knows something
	 * happened. Reset the device and reallocate resources ...
	 */

	switch (Direction) {
	case XEMACPS_RECV:
		if (ErrorWord & XEMACPS_RXSR_HRESPNOK_MASK) {
			xil_printf("Receive DMA error\n\r");
		}
		if (ErrorWord & XEMACPS_RXSR_RXOVR_MASK) {
			xil_printf("Receive over run\n\r");
		}
		if (ErrorWord & XEMACPS_RXSR_BUFFNA_MASK) {
			xil_printf("Receive buffer not available\n\r");
		}
		break;
	case XEMACPS_SEND:
		if (ErrorWord & XEMACPS_TXSR_HRESPNOK_MASK) {
			xil_printf("Transmit DMA error\n\r");
		}
		if (ErrorWord & XEMACPS_TXSR_URUN_MASK) {
			xil_printf("Transmit under run\n\r");
		}
		if (ErrorWord & XEMACPS_TXSR_BUFEXH_MASK) {
			xil_printf("Transmit buffer exhausted\n\r");
		}
		if (ErrorWord & XEMACPS_TXSR_RXOVR_MASK) {
			xil_printf("Transmit retry excessed limits\n\r");
		}
		if (ErrorWord & XEMACPS_TXSR_FRAMERX_MASK) {
			xil_printf("Transmit collision\n\r");
		}
		if (ErrorWord & XEMACPS_TXSR_USEDREAD_MASK) {
			xil_printf("Transmit buffer not available\n\r");
		}
		break;
	}
	/*
	 * Bypassing the reset functionality as the default tx status for q0 is
	 * USED BIT READ. so, the first interrupt will be tx used bit and it resets
	 * the core always.
	 */
	EmacPsResetDevice(EmacPsInstancePtr);
}

/****************************************************************************/
/**
* This function resets the device but preserves the options set by the user.
*
* The descriptor list could be reinitialized with the same calls to
* XEmacPs_BdRingClone() as used in main(). Doing this is a matter of
* preference.
* In many cases, an OS may have resources tied up in the descriptors.
* Reinitializing in this case may bad for the OS since its resources may be
* permamently lost.
*
* @param	EmacPsInstancePtr is a pointer to the instance of the EmacPs
*		driver.
*
* @return	XST_SUCCESS if successful, else XST_FAILURE.
*
* @note		None.
*
*****************************************************************************/
static LONG EmacPsResetDevice(XEmacPs * EmacPsInstancePtr)
{
	LONG Status = 0;
	u8 MacSave[6];
	u32 Options;
	XEmacPs_Bd BdTemplate;


	/*
	 * Stop device
	 */
	XEmacPs_Stop(EmacPsInstancePtr);

	/*
	 * Save the device state
	 */
	XEmacPs_GetMacAddress(EmacPsInstancePtr, &MacSave, 1);
	Options = XEmacPs_GetOptions(EmacPsInstancePtr);

	/*
	 * Stop and reset the device
	 */
	XEmacPs_Reset(EmacPsInstancePtr);

	/*
	 * Restore the state
	 */
	XEmacPs_SetMacAddress(EmacPsInstancePtr, &MacSave, 1);
	Status |= XEmacPs_SetOptions(EmacPsInstancePtr, Options);
	Status |= XEmacPs_ClearOptions(EmacPsInstancePtr, ~Options);
	if (Status != XST_SUCCESS) {
		xil_printf("Error restoring state after reset\n\r");
		return XST_FAILURE;
	}

	/*
	 * Setup callbacks
	 */
	Status = XEmacPs_SetHandler(EmacPsInstancePtr,
				     XEMACPS_HANDLER_DMASEND,
				     (void *) XEmacPsSendHandler,
				     EmacPsInstancePtr);
	Status |= XEmacPs_SetHandler(EmacPsInstancePtr,
				    XEMACPS_HANDLER_DMARECV,
				    (void *) XEmacPsRecvHandler,
				    EmacPsInstancePtr);
	Status |= XEmacPs_SetHandler(EmacPsInstancePtr, XEMACPS_HANDLER_ERROR,
				    (void *) XEmacPsErrorHandler,
				    EmacPsInstancePtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Error assigning handlers\n\r");
		return XST_FAILURE;
	}

	/*
	 * Setup RxBD space.
	 *
	 * We have already defined a properly aligned area of memory to store
	 * RxBDs at the beginning of this source code file so just pass its
	 * address into the function. No MMU is being used so the physical and
	 * virtual addresses are the same.
	 *
	 * Setup a BD template for the Rx channel. This template will be copied
	 * to every RxBD. We will not have to explicitly set these again.
	 */
	XEmacPs_BdClear(&BdTemplate);

	/*
	 * Create the RxBD ring
	 */
	Status = XEmacPs_BdRingCreate(&(XEmacPs_GetRxRing
				      (EmacPsInstancePtr)),
				      (UINTPTR) RxBdSpacePtr,
				      (UINTPTR) RxBdSpacePtr,
				      XEMACPS_BD_ALIGNMENT,
				      RXBD_CNT);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up RxBD space, BdRingCreate\n\r");
		return XST_FAILURE;
	}

	Status = XEmacPs_BdRingClone(&
				      (XEmacPs_GetRxRing(EmacPsInstancePtr)),
				      &BdTemplate, XEMACPS_RECV);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up RxBD space, BdRingClone\n\r");
		return XST_FAILURE;
	}

	/*
	 * Setup TxBD space.
	 *
	 * Like RxBD space, we have already defined a properly aligned area of
	 * memory to use.
	 *
	 * Also like the RxBD space, we create a template. Notice we don't set
	 * the "last" attribute. The examples will be overriding this
	 * attribute so it does no good to set it up here.
	 */
	XEmacPs_BdClear(&BdTemplate);
	XEmacPs_BdSetStatus(&BdTemplate, XEMACPS_TXBUF_USED_MASK);

	/*
	 * Create the TxBD ring
	 */
	Status = XEmacPs_BdRingCreate(&(XEmacPs_GetTxRing
				      (EmacPsInstancePtr)),
				      (UINTPTR) TxBdSpacePtr,
				      (UINTPTR) TxBdSpacePtr,
				      XEMACPS_BD_ALIGNMENT,
				      TXBD_CNT);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up TxBD space, BdRingCreate\n\r");
		return XST_FAILURE;
	}
	Status = XEmacPs_BdRingClone(&
				      (XEmacPs_GetTxRing(EmacPsInstancePtr)),
				      &BdTemplate, XEMACPS_SEND);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up TxBD space, BdRingClone\n\r");
		return XST_FAILURE;
	}

	/*
	 * Restart the device
	 */
	XEmacPs_Start(EmacPsInstancePtr);

	return XST_SUCCESS;
}

/****************************************************************************/
/**
*
* This function detects the PHY address by looking for successful MII status
* register contents.
*
* @param    The XEMACPS driver instance
*
* @return   The address of the PHY (defaults to 32 if none detected)
*
* @note     None.
*
*****************************************************************************/

u32 XEmacPsDetectPHY(XEmacPs * EmacPsInstancePtr)
{
	u32 PhyAddr;
	u32 Status;
	u16 PhyReg1;
	u16 PhyReg2;

	for (PhyAddr = 0; PhyAddr <= 31; PhyAddr++) {
		Status = XEmacPs_PhyRead(EmacPsInstancePtr, PhyAddr,
					  PHY_DETECT_REG1, &PhyReg1);

		Status |= XEmacPs_PhyRead(EmacPsInstancePtr, PhyAddr,
					   PHY_DETECT_REG2, &PhyReg2);

		if ((Status == XST_SUCCESS) &&
		    (PhyReg1 > 0x0000) && (PhyReg1 < 0xffff) &&
		    (PhyReg2 > 0x0000) && (PhyReg2 < 0xffff)) {
			/* Found a valid PHY address */
			return PhyAddr;
		}
	}

	return PhyAddr;		/* default to 32(max of iteration) */
}

LONG phyConfig(XEmacPs * macPtr, u32 speed) {

    u32 PhyAddr = 0;
    u32 Status = 0;
    u32 PhyIdentity = 0;
    u16 PhyReg0;

	XEmacPs_PhyRead(macPtr, PhyAddr, 0, &PhyReg0);
	PhyReg0 |= PHY_REG0_RESET;
	XEmacPs_PhyWrite(macPtr, PhyAddr, 0, PhyReg0);
    for(u32 i=0; i < 0xfffff; i++);
        sleep(1);

//	PhyReg0 |= PHY_REG0_LOOPBACK;
//	PhyReg0 &= ~PHY_REG0_RESET;
//	XEmacPs_PhyWrite(EmacPsInstancePtr, PhyAddr, 0, PhyReg0);

	XEmacPs_PhyRead(macPtr, PhyAddr, PHY_DETECT_REG1, &PhyIdentity);
	//Auto negotiate and Link up
	if (PhyIdentity == PHY_ID_RTL) {
		Status = phyAutoNegotiate(macPtr, PhyAddr);
		Status |= phyLinkStatus(macPtr, PhyAddr);
	}

	return Status;
}
LONG phyAutoNegotiate(XEmacPs * macPtr, u32 PhyAddr) {
	LONG Status = 0;
	u16 auto_negotiate = 0;
	u16 try = 0;
	u16 PhyReg1;

	while (!auto_negotiate & (try < 10)) {
		XEmacPs_PhyRead(macPtr, PhyAddr, 1, &PhyReg1);
		auto_negotiate = PhyReg1 & PHY_REG1_AUTONEG;
	    for(u32 i=0; i < 0xff; i++);
	        sleep(1);
		try++;
	}

	if (auto_negotiate) {
		xil_printf("Auto Negotiation Complete\n\r");
		Status = XST_SUCCESS;
	} else {
		xil_printf("Auto Negotiation Failed\n\r");
	}

	return Status;
}

LONG phyLinkStatus(XEmacPs * macPtr, u32 PhyAddr) {
	LONG Status = 0;
	u16 link_status = 0;
	u16 try = 0;
	u16 PhyReg1;

	while (!link_status & (try < 10)) {
		XEmacPs_PhyRead(macPtr, PhyAddr, 1, &PhyReg1);
		link_status = PhyReg1 & PHY_REG1_LINKSTS;
	    for(u32 i=0; i < 0xff; i++);
	        sleep(1);
		try++;
	}

	if (link_status) {
		xil_printf("Link Established\n\r");
		Status = XST_SUCCESS;
	} else {
		xil_printf("Link Not Established\n\r");
	}
	
	return Status;
}

/****************************************************************************/
/**
*
* This function setups the interrupt system so interrupts can occur for the
* EMACPS.
* @param	IntcInstancePtr is a pointer to the instance of the Intc driver.
* @param	EmacPsInstancePtr is a pointer to the instance of the EmacPs
*		driver.
* @param	EmacPsIntrId is the Interrupt ID and is typically
*		XPAR_<EMACPS_instance>_INTR value from xparameters.h.
*
* @return	XST_SUCCESS if successful, otherwise XST_FAILURE.
*
* @note		None.
*
*****************************************************************************/
static LONG EmacPsSetupIntrSystem(INTC *IntcInstancePtr,
				  XEmacPs *EmacPsInstancePtr,
				  u16 EmacPsIntrId)
{
	LONG Status;

	XScuGic_Config *GicConfig;
	Xil_ExceptionInit();

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use.
	 */
	GicConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == GicConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(IntcInstancePtr, GicConfig,
		    GicConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}


	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the processor.
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT,
			(Xil_ExceptionHandler)XScuGic_InterruptHandler,
			IntcInstancePtr);

	/*
	 * Connect a device driver handler that will be called when an
	 * interrupt for the device occurs, the device driver handler performs
	 * the specific interrupt processing for the device.
	 */
	Status = XScuGic_Connect(IntcInstancePtr, EmacPsIntrId,
			(Xil_InterruptHandler) XEmacPs_IntrHandler,
			(void *) EmacPsInstancePtr);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Unable to connect ISR to interrupt controller\n\r");
		return XST_FAILURE;
	}

	/*
	 * Enable interrupts from the hardware
	 */
	XScuGic_Enable(IntcInstancePtr, EmacPsIntrId);
	/*
	 * Enable interrupts in the processor
	 */
	Xil_ExceptionEnable();

	return XST_SUCCESS;
}

LONG macInit(XEmacPs * macInstPtr, u16 * macIntrId, XEmacPs_Config * macConfigInstPtr, INTC * IntcInstancePtr) {
	LONG Status = 0;
	u32 gemVersion;
	XEmacPs_Config *macConfig = macConfigInstPtr;
	XEmacPs *macPtr = macInstPtr;
	INTC * IntcPtr = IntcInstancePtr;
	

	//Initialize the MAC
    macConfig = XEmacPs_LookupConfig(XPAR_XEMACPS_0_DEVICE_ID);
	Status = XEmacPs_CfgInitialize(macPtr, macConfig, macConfig->BaseAddress);
	if (Status != XST_SUCCESS) {
		xil_printf("Error initializing MAC\n\r");
		return XST_FAILURE;
	}
	else {
		xil_printf("MAC Initialized\n\r");
	}

	//Interrupt ID
	*macIntrId = XPAR_XEMACPS_0_INTR;
	
    gemVersion = ((Xil_In32(macConfig->BaseAddress + 0xFC)) >> 16) & 0xFFF;
    
	//Configure the Controller
	XEmacPsClkSetup(macPtr);



	/*
	 * Set the MAC address
	 */
	Status = XEmacPs_SetMacAddress(macPtr, srcMac, 1);
	if (Status != XST_SUCCESS) {
		xil_printf("Error setting MAC address\n\r");
		return XST_FAILURE;
	}

	/*
	 * Setup callbacks
	 */
	Status = XEmacPs_SetHandler(macPtr,
				     XEMACPS_HANDLER_DMASEND,
				     (void *) XEmacPsSendHandler,
					 macPtr);
	Status |=
		XEmacPs_SetHandler(macPtr,
				    XEMACPS_HANDLER_DMARECV,
				    (void *) XEmacPsRecvHandler,
					macPtr);
	Status |=
		XEmacPs_SetHandler(macPtr, XEMACPS_HANDLER_ERROR,
				    (void *) XEmacPsErrorHandler,
					macPtr);

	if (Status != XST_SUCCESS) {
		xil_printf("Error assigning handlers\n\r");
		return XST_FAILURE;
	}
	xil_printf("Handlers assigned\n\r");

	//Setup the interrupt system
	Status = EmacPsSetupIntrSystem(IntcPtr, macPtr, *macIntrId);
	if (Status != XST_SUCCESS) {
		xil_printf("Error setting up interrupt system\n\r");
		return XST_FAILURE;
	}


	//Maximum divisor, CPU clock is 667MHz
    XEmacPs_SetMdioDivisor(macPtr, MDC_DIV_224);
    XEmacPs_SetOperatingSpeed(macPtr, EMACPS_LOOPBACK_SPEED_1G);
    sleep(1);

    //Auto negotiate and Establish Link
    phyConfig(macPtr, EMACPS_LOOPBACK_SPEED_1G);

    //Disable Broadcast
    u32 regCfg = XEmacPs_ReadReg(macPtr->Config.BaseAddress, XEMACPS_NWCFG_OFFSET);
    regCfg |= XEMACPS_NWCFG_BCASTDI_MASK;
    XEmacPs_WriteReg(macPtr->Config.BaseAddress, XEMACPS_NWCFG_OFFSET, regCfg);
    regCfg = XEmacPs_ReadReg(macPtr->Config.BaseAddress, XEMACPS_NWCFG_OFFSET);
    xil_printf("Network Configuration Register: 0x%08X\n\r", regCfg);
    
    //Set DMA Receive Buffer size to 0x30 = 48 => 3072 Bytes or 2 ethernet frames
    u32 dmaCfg = XEmacPs_ReadReg(macPtr->Config.BaseAddress, 0x10);
    dmaCfg &= ~(0xFF << 16);
    dmaCfg |= (0x30 << 16);
    XEmacPs_WriteReg(macPtr->Config.BaseAddress, 0x10, dmaCfg);

    //Set TypeID 0x0FFF
    XEmacPs_WriteReg(macPtr->Config.BaseAddress, 0xA8, 0x80000FFF);

    //Set MAC Loopback
	// *((volatile u32*) macPtr->Config.BaseAddress) |= 0x2;
	xil_printf("Mac Initialization Complete\n\r");
	return XST_SUCCESS;

}

LONG bdInit(XEmacPs * macInstPtr) {
	LONG Status = 0;
	XEmacPs *macPtr = macInstPtr;
    XEmacPs_Bd BdTemplate;
    XEmacPs_Bd * bdRxPtr;

	/*
	* The BDs need to be allocated in uncached memory. Hence the 1 MB
	* address range that starts at "bd_space" is made uncached.
    */
    Xil_SetTlbAttributes((INTPTR)bd_space, DEVICE_MEMORY);

	/* Allocate Rx and Tx BD space each */
	RxBdSpacePtr = &(bd_space[0]);
	TxBdSpacePtr = &(bd_space[0x10000]);

    /*
	 * Setup RxBD space.
	 *
	 * We have already defined a properly aligned area of memory to store
	 * RxBDs at the beginning of this source code file so just pass its
	 * address into the function. No MMU is being used so the physical
	 * and virtual addresses are the same.
	 *
	 * Setup a BD template for the Rx channel. This template will be
	 * copied to every RxBD. We will not have to explicitly set these
	 * again.
	 */
	XEmacPs_BdClear(&BdTemplate);

	xil_printf("Setting up RX BD Space\n\r");
	/*
	 * Create the RxBD ring
	 */
	Status = XEmacPs_BdRingCreate(&(XEmacPs_GetRxRing
				       (macPtr)),
				       (UINTPTR) RxBdSpacePtr,
				       (UINTPTR) RxBdSpacePtr,
				       XEMACPS_BD_ALIGNMENT,
				       RXBD_CNT);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up RxBD space, BdRingCreate\n\r");
		return XST_FAILURE;
	}

	Status = XEmacPs_BdRingClone(&(XEmacPs_GetRxRing(macPtr)),
				      &BdTemplate, XEMACPS_RECV);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up RxBD space, BdRingClone\n\r");
		return XST_FAILURE;
	}

	bdRxPtr = (XEmacPs_Bd *) &macPtr->RxBdRing.BaseBdAddr;
	SetRxBuf(macPtr);
    Status = XEmacPs_BdRingToHw(&(XEmacPs_GetRxRing(macPtr)),
                RXBD_CNT, bdRxPtr);

    XEmacPs_SetQueuePtr(macPtr, macPtr->RxBdRing.BaseBdAddr, 0, XEMACPS_RECV);
	/*
	 * Setup TxBD space.
	 *
	 * Like RxBD space, we have already defined a properly aligned area
	 * of memory to use.
	 *
	 * Also like the RxBD space, we create a template. Notice we don't
	 * set the "last" attribute. The example will be overriding this
	 * attribute so it does no good to set it up here.
	 */

    xil_printf("Setting up TX BD Space\n\r");
	XEmacPs_BdClear(&BdTemplate);
	XEmacPs_BdSetStatus(&BdTemplate, XEMACPS_TXBUF_USED_MASK);

	/*
	 * Create the TxBD ring
	 */

	Status = XEmacPs_BdRingCreate(&(XEmacPs_GetTxRing
				       (macPtr)),
				       (UINTPTR) TxBdSpacePtr,
				       (UINTPTR) TxBdSpacePtr,
				       XEMACPS_BD_ALIGNMENT,
				       TXBD_CNT);
                       
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up TxBD space, BdRingCreate\n\r");
		return XST_FAILURE;
	}

	Status = XEmacPs_BdRingClone(&(XEmacPs_GetTxRing(macPtr)),
				      &BdTemplate, XEMACPS_SEND);
	if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting up TxBD space, BdRingClone\n\r");
		return XST_FAILURE;
	}

	return Status;
}
