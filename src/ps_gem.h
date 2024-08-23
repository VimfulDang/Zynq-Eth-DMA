#include "xparameters.h"
#include "xemacps.h"

/*
    MAC Registers
*/

#define XEMACPS_NWCFG_OFFSET            0x00000004

/*
    Network Configuration Register
*/
#define XEMACPS_NWCFG_MDCCLKDIV_MASK    0x00070000

/*
 * SLCR setting
 */

#define SLCR_LOCK_KEY_VALUE		0x767B
#define SLCR_UNLOCK_KEY_VALUE		0xDF0D

#define SLCR_LOCK_ADDR			(XPS_SYS_CTRL_BASEADDR + 0x4)
#define SLCR_UNLOCK_ADDR		(XPS_SYS_CTRL_BASEADDR + 0x8)
#define SLCR_GEM0_CLK_CTRL_ADDR		(XPS_SYS_CTRL_BASEADDR + 0x140)

#define EMACPS_SLCR_DIV_MASK	0xFC0FC0FF

/*
    Function Prototypes
*/
//LONG XEmacPs_SetMacAddress(XEmacPs *InstancePtr, void *AddressPtr, u8 Index);
void XEmacPsClkSetup(XEmacPs *EmacPsInstancePtr);
static void XEmacPsSendHandler(void *Callback);
static void XEmacPsRecvHandler(void *Callback);
static void XEmacPsErrorHandler(void *Callback, u8 Direction, u32 ErrorWord);

///**  @name MDC clock division
// *  currently supporting 8, 16, 32, 48, 64, 96, 128, 224.
// * @{
// */
//typedef enum { MDC_DIV_8 = 0U, MDC_DIV_16, MDC_DIV_32, MDC_DIV_48,
//	MDC_DIV_64, MDC_DIV_96, MDC_DIV_128, MDC_DIV_224
//} XEmacPs_MdcDiv;

void XEmacPs_SetMdioDivisor(XEmacPs *InstancePtr, XEmacPs_MdcDiv Divisor);

//
///*****************************************************************************/
///**
// * Set the MAC address for this driver/device.  The address is a 48-bit value.
// * The device must be stopped before calling this function.
// *
// * @param InstancePtr is a pointer to the instance to be worked on.
// * @param AddressPtr is a pointer to a 6-byte MAC address.
// * @param Index is a index to which MAC (1-4) address.
// *
// * @return
// * - XST_SUCCESS if the MAC address was set successfully
// * - XST_DEVICE_IS_STARTED if the device has not yet been stopped
// *
// *****************************************************************************/
//LONG XEmacPs_SetMacAddress(XEmacPs *InstancePtr, void *AddressPtr, u8 Index)
//{
//	u32 MacAddr;
//	u8 *Aptr = (u8 *)(void *)AddressPtr;
//	u8 IndexLoc = Index;
//	LONG Status;
//	Xil_AssertNonvoid(InstancePtr != NULL);
//	Xil_AssertNonvoid(Aptr != NULL);
//	Xil_AssertNonvoid(InstancePtr->IsReady == (u32)XIL_COMPONENT_IS_READY);
//	Xil_AssertNonvoid((IndexLoc <= (u8)XEMACPS_MAX_MAC_ADDR) && (IndexLoc > 0x00U));
//
//	/* Be sure device has been stopped */
//	if (InstancePtr->IsStarted == (u32)XIL_COMPONENT_IS_STARTED) {
//		Status = (LONG)(XST_DEVICE_IS_STARTED);
//	}
//	else{
//	/* Index ranges 1 to 4, for offset calculation is 0 to 3. */
//		IndexLoc--;
//
//	/* Set the MAC bits [31:0] in BOT */
//		MacAddr = *(Aptr);
//		MacAddr |= ((u32)(*(Aptr+1)) << 8U);
//		MacAddr |= ((u32)(*(Aptr+2)) << 16U);
//		MacAddr |= ((u32)(*(Aptr+3)) << 24U);
//	XEmacPs_WriteReg(InstancePtr->Config.BaseAddress,
//				((u32)XEMACPS_LADDR1L_OFFSET + ((u32)IndexLoc * (u32)8)), MacAddr);
//
//	/* There are reserved bits in TOP so don't affect them */
//	MacAddr = XEmacPs_ReadReg(InstancePtr->Config.BaseAddress,
//					((u32)XEMACPS_LADDR1H_OFFSET + ((u32)IndexLoc * (u32)8)));
//
//		MacAddr &= (u32)(~XEMACPS_LADDR_MACH_MASK);
//
//	/* Set MAC bits [47:32] in TOP */
//		MacAddr |= (u32)(*(Aptr+4));
//		MacAddr |= (u32)(*(Aptr+5)) << 8U;
//
//	XEmacPs_WriteReg(InstancePtr->Config.BaseAddress,
//				((u32)XEMACPS_LADDR1H_OFFSET + ((u32)IndexLoc * (u32)8)), MacAddr);
//
//		Status = (LONG)(XST_SUCCESS);
//	}
//	return Status;
//}

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

/*****************************************************************************/
/**
 * Set the MDIO clock divisor.
 *
 * Calculating the divisor:
 *
 * <pre>
 *              f[HOSTCLK]
 *   f[MDC] = -----------------
 *            (1 + Divisor) * 2
 * </pre>
 *
 * where f[HOSTCLK] is the bus clock frequency in MHz, and f[MDC] is the
 * MDIO clock frequency in MHz to the PHY. Typically, f[MDC] should not
 * exceed 2.5 MHz. Some PHYs can tolerate faster speeds which means faster
 * access. Here is the table to show values to generate MDC,
 *
 * <pre>
 * 000 : divide pclk by   8 (pclk up to  20 MHz)
 * 001 : divide pclk by  16 (pclk up to  40 MHz)
 * 010 : divide pclk by  32 (pclk up to  80 MHz)
 * 011 : divide pclk by  48 (pclk up to 120 MHz)
 * 100 : divide pclk by  64 (pclk up to 160 MHz)
 * 101 : divide pclk by  96 (pclk up to 240 MHz)
 * 110 : divide pclk by 128 (pclk up to 320 MHz)
 * 111 : divide pclk by 224 (pclk up to 540 MHz)
 * </pre>
 *
 * @param InstancePtr is a pointer to the instance to be worked on.
 * @param Divisor is the divisor to set. Range is 0b000 to 0b111.
 *
 *****************************************************************************/
void XEmacPs_SetMdioDivisor(XEmacPs *InstancePtr, XEmacPs_MdcDiv Divisor)
{
	u32 Reg;
	Xil_AssertVoid(InstancePtr != NULL);
	Xil_AssertVoid(InstancePtr->IsReady == (u32)XIL_COMPONENT_IS_READY);
	Xil_AssertVoid(Divisor <= (XEmacPs_MdcDiv)0x7); /* only last three bits are valid */

	Reg = XEmacPs_ReadReg(InstancePtr->Config.BaseAddress,
				XEMACPS_NWCFG_OFFSET);
	/* clear these three bits, could be done with mask */
	Reg &= (u32)(~XEMACPS_NWCFG_MDCCLKDIV_MASK);

	Reg |= ((u32)Divisor << XEMACPS_NWCFG_MDC_SHIFT_MASK);

	XEmacPs_WriteReg(InstancePtr->Config.BaseAddress,
			   XEMACPS_NWCFG_OFFSET, Reg);
}
