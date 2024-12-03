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

     //TODO: Need to setup RX BDs for receive and continuous process of frames
     /* Send ARP Message to Host Computer */
    Status = sendArpRequest(hostIp, macPtr);
    if (Status != XST_SUCCESS) {
        xil_printf("Error sending ARP Request\n\r");
        return XST_FAILURE;
    }
    xil_printf("Exiting\n\r");
    return 0;
}
