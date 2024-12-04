#include "eth_dma.h"
#include "ps_gem.h"

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


int main(void) {
    long Status;
    
    XEmacPs_Config *macConfig;
    XEmacPs * macPtr = &macInstance;
//    u32 gemVersion;
    u16 macIntrId = 0;
    INTC *IntcInstancePtr = &IntcInstance;

    /* Initialize the MAC */
    Status = macInit(macPtr, macIntrId, macConfig, IntcInstancePtr);
    if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting macInit\n\r");
		return XST_FAILURE;
	}
    /* Initialize Buffer Descriptor Rings*/
    Status = bdInit(macPtr);
    if (Status != XST_SUCCESS) {
        xil_printf
            ("Error setting bdInit\n\r");
        return XST_FAILURE;
    }

    /*
	 * Setup the interrupt controller and enable interrupts
	 */
	Status = EmacPsSetupIntrSystem(IntcInstancePtr,
					macPtr, macIntrId);
    XEmacPs_IntEnable(macPtr, (XEMACPS_IXR_FRAMERX_MASK | XEMACPS_IXR_RX_ERR_MASK));
     //TODO: Need to setup RX BDs for receive and continuous process of frames
     /* Send ARP Message to Host Computer */
    Status = sendArpRequest(hostIp, macPtr);
    // if (Status != XST_SUCCESS) {
    //     xil_printf("Error sending ARP Request\n\r");
    //     return XST_FAILURE;
    // }
    while ((FramesRx < 10) & (FramesRxErr < 1)) {
        if (FramesRx > RxProcessed) {
            xil_printf("FramesRx: %d\n\rRxProcessed: %d\n\r", FramesRx, RxProcessed);
            XEmacPs_Bd * bdPtr = (XEmacPs_Bd *) macPtr->RxBdRing.BaseBdAddr + macPtr->RxBdRing.Separation*RxProcessed;
            //Wraps bdPtr around
            if (((UINTPTR) bdPtr) > macPtr->RxBdRing.HighBdAddr) {
                bdPtr -= macPtr->RxBdRing.Length;
            }
            //Read the used bit and make sure it's asserted
            if (XEmacPs_BdIsRxNew(bdPtr)) {
                ethHdr * ethHdrPtr = (ethHdr *) XEmacPs_BdGetAddressRx(bdPtr);
                //Check if frame is an ARP packet
                if (ethHdrPtr->frame_type == htons(0x0808)) {
                    arpProcess((arpPkt *) (ethHdrPtr + 1), macPtr);
                }
                xil_printf("Frame Length: %d\n\r", XEmacPs_BdGetLength(bdPtr));
                RxProcessed++;
            }
            //Clear Used Bit
            XEmacPs_BdClearRxNew(bdPtr);
            sleep(1);
            Status = sendArpRequest(hostIp, macPtr);
        }
    }
    xil_printf("FramesRx: %d\n\rFramesRxErr: %d\n\r", FramesRx, FramesRxErr);
    xil_printf("Exiting\n\r");
    return 0;
}
