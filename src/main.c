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
    int arpCnt = 0;
    XEmacPs_Bd * bdPtr = ((XEmacPs_Bd *) macPtr->RxBdRing.BaseBdAddr);
    xil_printf("DMA Config at 0x10: 0x%08x\n\r", XEmacPs_ReadReg(macPtr->Config.BaseAddress, 0x10));
    while (arpCnt < 10) {
        while (RxProcessed < FramesRx) {
            xil_printf("RxProcessed: %d, FramesRx: %d ArpCnt: %d\n\r", RxProcessed, FramesRx, arpCnt);
            //Wraps bdPtr around
            if (((UINTPTR) bdPtr) > macPtr->RxBdRing.HighBdAddr) {
                bdPtr = ((UINTPTR) bdPtr) -  macPtr->RxBdRing.Length;
            }
            //Read the used bit and make sure it's asserted
            if (XEmacPs_BdIsRxNew(bdPtr)) {
                ethHdr_t * ethHdrPtr = (ethHdr_t *) (XEmacPs_BdGetBufAddr(bdPtr) & XEMACPS_RXBUF_ADD_MASK);
                xil_printf("Frame Length: %d\n\r", XEmacPs_BdGetLength(bdPtr));
                u8 *payloadPtr = (u8 *) (ethHdrPtr + 1);
                xil_printf("Payload: ");
                for (int i = 0; i < XEmacPs_BdGetLength(bdPtr) - sizeof(ethHdr_t); i++) {
                    xil_printf("%02x ", payloadPtr[i]);
                }
                xil_printf("\n\r");
                xil_printf("Frame Type: 0x%04x\n\r", ntohs(ethHdrPtr->frame_type));
                if (ethHdrPtr->frame_type == ntohs(0x0806)) {
                    //Skip past the ethernet header
                    arpPkt_t * arpPtr = (arpPkt_t *) (((XEmacPs_BdGetBufAddr(bdPtr) & XEMACPS_RXBUF_ADD_MASK)) + sizeof(ethHdr_t));
                    //Returns the difference of the first differing byte as if they're unsigned bytes
                    if (memcmp(arpPtr->spa, hostIp, sizeof(arpPtr->spa)) == 0) {
                        memcpy((void*) hostMac, arpPtr->sha, sizeof(arpPtr->sha));
                        //Set MAC Filter for host
                        arpCnt++;
                    }
                }
                XEmacPs_BdClearRxNew(bdPtr);
                RxProcessed++;
                bdPtr++;
            }
        }
        // sleep(5);
        // Status = sendArpRequest(hostIp, macPtr);
    }
	XEmacPs_IntDisable(macPtr, (XEMACPS_IXR_FRAMERX_MASK |
		XEMACPS_IXR_RX_ERR_MASK));
    xil_printf("FramesRx: %d\n\rFramesRxErr: %d\n\r", FramesRx, FramesRxErr);
    xil_printf("Exiting\n\r");
    return 0;
}
