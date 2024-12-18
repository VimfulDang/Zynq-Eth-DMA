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
    while (arpCnt < 10) {
        while (FramesRx > RxProcessed) {
            // xil_printf("FramesRx: %d\n\rRxProcessed: %d\n\r", FramesRx, RxProcessed);
            XEmacPs_Bd * bdPtr = ((XEmacPs_Bd *) macPtr->RxBdRing.BaseBdAddr) + (RxProcessed % macPtr->RxBdRing.Length);
            // xil_printf("BaseAddr: %p\tbdPtr:%p\n\r", macPtr->RxBdRing.BaseBdAddr, bdPtr);
            //Wraps bdPtr around
            if (((UINTPTR) bdPtr) > macPtr->RxBdRing.HighBdAddr) {
                bdPtr -= macPtr->RxBdRing.Length;
            }
            //Read the used bit and make sure it's asserted
            if (XEmacPs_BdIsRxNew(bdPtr)) {
                ethHdr_t * ethHdrPtr = (ethHdr_t *) (XEmacPs_BdGetBufAddr(bdPtr) & XEMACPS_RXBUF_ADD_MASK);
                // xil_printf("Buffer Address: %p\n\r", ethHdrPtr);
                //Check if frame is an ARP packet
                xil_printf("Destination MAC: %02x:%02x:%02x:%02x:%02x:%02x\n\r",
                           ethHdrPtr->dest_addr[0], ethHdrPtr->dest_addr[1], ethHdrPtr->dest_addr[2],
                           ethHdrPtr->dest_addr[3], ethHdrPtr->dest_addr[4], ethHdrPtr->dest_addr[5]);
                xil_printf("Source MAC: %02x:%02x:%02x:%02x:%02x:%02x\n\r",
                           ethHdrPtr->src_addr[0], ethHdrPtr->src_addr[1], ethHdrPtr->src_addr[2],
                           ethHdrPtr->src_addr[3], ethHdrPtr->src_addr[4], ethHdrPtr->src_addr[5]);
                xil_printf("ETH Type: 0x%04x\n\r", ntohs(ethHdrPtr->frame_type));
                if (ethHdrPtr->frame_type == ntohs(0x0806)) {
//                    arpProcess((arpPkt_t *) (ethHdrPtr + 1), macPtr);
                	xil_printf("Found ARP Frame\n\r");
                    arpCnt++;
                }
                // if (XEmacPs_BdIsLast(bdPtr))
                	// xil_printf("Bd has last\n\r");
                // xil_printf("Frame Length: %d\n\r", XEmacPs_BdGetLength(bdPtr));
                XEmacPs_BdClearRxNew(bdPtr);
                RxProcessed++;
            }
            //Clear Used Bit
        }
        sleep(5);
        Status = sendArpRequest(hostIp, macPtr);
    }
	XEmacPs_IntDisable(macPtr, (XEMACPS_IXR_FRAMERX_MASK |
		XEMACPS_IXR_RX_ERR_MASK));
    xil_printf("FramesRx: %d\n\rFramesRxErr: %d\n\r", FramesRx, FramesRxErr);
    xil_printf("Exiting\n\r");
    return 0;
}
