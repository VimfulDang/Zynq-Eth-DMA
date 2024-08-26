#include "xparameters.h"
#include "xemacps.h"
#include "xil_exception.h"



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
    Function Prototypes
*/
//LONG XEmacPs_SetMacAddress(XEmacPs *InstancePtr, void *AddressPtr, u8 Index);
void XEmacPsClkSetup(XEmacPs *EmacPsInstancePtr);
static void XEmacPsSendHandler(void *Callback);
static void XEmacPsRecvHandler(void *Callback);
static void XEmacPsErrorHandler(void *Callback, u8 Direction, u32 ErrorWord);
static LONG EmacPsResetDevice(XEmacPs * EmacPsInstancePtr);


LONG phyConfig(XEmacPs * macPtr, u32 speed);
LONG phyAutoNegotiate(XEmacPs * macPtr, u32 PhyAddr);
LONG phyLinkStatus(XEmacPs * macPtr, u32 PhyAddr);

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
//	FramesTx++;
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
	XEmacPs *EmacPsInstancePtr = (XEmacPs *) Callback;

	/*
	 * Disable the transmit related interrupts
	 */
	XEmacPs_IntDisable(EmacPsInstancePtr, (XEMACPS_IXR_FRAMERX_MASK |
		XEMACPS_IXR_RX_ERR_MASK));
	/*
	 * Increment the counter so that main thread knows something
	 * happened.
	 */
//	FramesRx++;
	if (EmacPsInstancePtr->Config.IsCacheCoherent == 0) {
		Xil_DCacheInvalidateRange((UINTPTR)&RxFrame, sizeof(EthernetFrame));
	}
//	xil_printf("RX Frame: %u\n\r", FramesRx);
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
	LONG Status = 0;
	u32 PhyAddr = 0;
	u16 PhyIdentity;

	//Loop through 32 addresses and return first valid PHY address by reading REG1 & REG2
	PhyAddr = XEmacPsDetectPHY(macPtr);
	XEmacPs_PhyRead(macPtr, PhyAddr, PHY_DETECT_REG1, &PhyIdentity);
	
	xil_printf("PHY ID: 0x%x\n\rPHY ADDR: 0x%x\n\r", PhyIdentity, PhyAddr);

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

	while (!auto_negotiate & try < 10) {
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

	while (!link_status & try < 10) {
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
