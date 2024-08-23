#include "eth_dma.h"
#include "ps_gem.h"
#include "xil_mmu.h"

/*
    1. Initialize the Controller
    2. Configure the Controller
    3. I/O Configuration
    4. Configure the PHY
    5. Configure the Buffer Descriptors
    6. Configure the interrupts
    7. Enable the controller
    8. Transmitting Frames
    9. Receiving Frames
*/


int main() {
    long Status;
    /* Initialize the MAC */
    XEmacPs_Config *macConfig;
    XEmacPs * macPtr;
    u32 gemVersion;
    u16 macIntrId;
    
    macConfig = XEmacPs_LookupConfig(XPAR_XEMACPS_0_DEVICE_ID);
    Status = XEmacPs_CfgInitialize(macPtr, macConfig, macConfig->BaseAddress);

    //Interrupt ID
    macIntrId = XPS_GEM0_INT_ID;

    //Configure the Controller
    macNwcfgConfig(macPtr);

    gemVersion = ((Xil_In32(macConfig->BaseAddress + 0xFC)) >> 16) & 0xFFF;
    
    //Configure MAC Clock
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

    /*
        * The BDs need to be allocated in uncached memory. Hence the 1 MB
        * address range that starts at "bd_space" is made uncached.
    */
    Xil_SetTlbAttributes((INTPTR)bd_space, DEVICE_MEMORY);

	/* Allocate Rx and Tx BD space each */
	RxBdSpacePtr = &(bd_space[0]);
	TxBdSpacePtr = &(bd_space[0x10000]);

    //Maximum divisor, CPU clock is 667MHz
    XEmacPs_SetMdioDivisor(macPtr, MDC_DIV_224);
    EmacpsDelay(1);

    //Auto negotiate and Establish Link
    // phyConfig(EmacPsInstancePtr);
    // phyAutoNegotiate(EmacPsInstancePtr);
    // phyLinkStatus(EmacPsInstancePtr);

    return 0;
}
