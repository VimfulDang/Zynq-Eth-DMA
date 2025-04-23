#include "eth_dma.h"
#include "ps_gem.h" // Includes xscugic.h indirectly
#include "axidma.h" // Includes xaxidma.h
#include "xparameters.h" // Needed for interrupt IDs
#include "xil_exception.h"
#include "xil_cache.h"
#include "sleep.h" // Use Xilinx sleep
//#include "xuartps.h"

#define MAX_FRAMES_TO_PROCESS 10
#define WAIT_TIMEOUT_SECONDS 5

// Flags for ISRs to signal main loop
static volatile int TxDone;
static volatile int RxDone;
static volatile int Error;

// --- Global Variable Definitions ---
// IMPORTANT: Define these variables ONLY ONCE in your entire project.
// If defined in another .c file (e.g., axidma.c), use 'extern' declarations here instead.
// Note: XAxiDma AxiDma is already defined globally in axidma.h in this case.
//       If you intended to define it here, remove the definition from axidma.h
//       and uncomment the line below, OR declare it 'extern' here if defined elsewhere.
// XAxiDma AxiDma;
XEmacPs macInstance;
XScuGic IntcInstance; // Assuming INTC is XScuGic based on ps_gem.h

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
    XEmacPs_Config *macConfig = XEmacPs_LookupConfig(XPAR_XEMACPS_0_DEVICE_ID);
    // Use the globally defined instances directly
    XEmacPs * macPtr = &macInstance;
    INTC *IntcInstancePtr = &IntcInstance; // Use the global IntcInstance
    u16 macIntrId = XPAR_XEMACPS_0_INTR; // Use interrupt ID from xparameters.h

    xil_printf("--- Entering Main ---\n\r");

    /* Initialize the Ethernet MAC */
    xil_printf("Initializing Ethernet MAC...\n\r");
    // Pass address of macIntrId as macInit expects a pointer
    Status = macInit(macPtr, &macIntrId, macConfig, IntcInstancePtr);
    if (Status != XST_SUCCESS) {
		xil_printf
			("Error setting macInit\n\r");
		return XST_FAILURE;
	}
    /* Initialize Ethernet Buffer Descriptor Rings*/
    xil_printf("Initializing Ethernet BDs...\n\r");
    Status = bdInit(macPtr);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Ethernet BD Initialization failed (Status: %ld)\n\r", Status);
        return XST_FAILURE;
    }
    xil_printf("Ethernet BDs Initialized Successfully.\n\r");

    /* Initialize AXI DMA */
    xil_printf("Initializing AXI DMA...\n\r");
    // axiDmaInit uses the global AxiDma instance defined in axidma.h
    Status = axiDmaInit();
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: AXI DMA Initialization failed (Status: %ld)\n\r", Status);
        return XST_FAILURE;
    }
    xil_printf("AXI DMA Initialized Successfully.\n\r");

    /*
	 * Setup the Ethernet interrupt controller and enable interrupts
     * (This part remains from your original code. Its necessity depends
     * on whether you need concurrent Ethernet PHY operations/interrupts.)
	 */
    xil_printf("Setting up Ethernet Interrupts (if needed)...\n\r");
    // Pass the value macIntrId as EmacPsSetupIntrSystem expects a value
	Status = EmacPsSetupIntrSystem(IntcInstancePtr, macPtr, macIntrId);
    if (Status != XST_SUCCESS) {
        xil_printf("Warning: Failed to set up Ethernet Interrupt System (Status: %ld)\n\r", Status);
        // Decide if this is fatal
        // return XST_FAILURE;
    } else {
        xil_printf("Ethernet Interrupt System Setup Complete.\n\r");
    }

    // Original Ethernet RX interrupt enable/disable sequence.
    // Consider if this is needed for your AXI DMA use case.
    XEmacPs_IntEnable(macPtr, (XEMACPS_IXR_FRAMERX_MASK | XEMACPS_IXR_RX_ERR_MASK));
    XEmacPs_RxEnable(macPtr);
	XEmacPs_IntDisable(macPtr, (XEMACPS_IXR_FRAMERX_MASK | XEMACPS_IXR_RX_ERR_MASK));

    // These counters relate to Ethernet PHY RX, not AXI DMA TX.
    xil_printf("Ethernet FramesRx: %u\n\rEthernet FramesRxErr: %u\n\r", (unsigned int)FramesRx, (unsigned int)FramesRxErr);

    xil_printf("Ethernet FramesRx: %u\n\rEthernet FramesRxErr: %u\n\r", (unsigned int)FramesRx, (unsigned int)FramesRxErr);

    /* Setup AXI DMA Interrupts */
    xil_printf("Setting up AXI DMA Interrupts...\n\r");
    // Note: GIC is already initialized by EmacPsSetupIntrSystem called within macInit
    Status = SetupAxiDmaInterrupts(IntcInstancePtr, &AxiDma);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Failed to set up AXI DMA interrupts (Status: %ld)\n\r", Status);
        return XST_FAILURE;
    }
    xil_printf("AXI DMA Interrupts Setup Complete.\n\r");

    // Initialize flags
    TxDone = 0;
    RxDone = 0;
    Error = 0;
    int timeout_counter = 0;

    /* Send packets using AXI DMA */
    xil_printf("Sending packets via AXI DMA...\n\r");
    // Pass the address of the global AxiDma instance (defined in axidma.h)
    Status = SendPackets(&AxiDma);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Failed to submit AXI DMA packets to hardware (Status: %ld)\n\r", Status);
        // Decide if this is fatal for your application
        // return XST_FAILURE;
    } else {
        xil_printf("AXI DMA packets submitted to hardware for transmission.\n\r");
        // Note: SendPackets just queues the transfer. It doesn't wait for completion.
        // You would typically need interrupt handlers or polling to check TX completion status.
    }

    xil_printf("Waiting for TX completion or RX packet (Timeout: %d seconds)...\n\r", WAIT_TIMEOUT_SECONDS);

    // Main loop waiting for interrupts with timeout
    while (!TxDone && !RxDone && !Error && (timeout_counter < WAIT_TIMEOUT_SECONDS)) {
        /* Wait */
        sleep(1); // Sleep for 1 second
        timeout_counter++;
    }

    // Check exit conditions
    if (Error) {
        xil_printf("AXI DMA Error occurred!\n\r");
    } else if (timeout_counter >= WAIT_TIMEOUT_SECONDS) {
        xil_printf("Timeout waiting for AXI DMA completion.\n\r");
        if (!TxDone) {
            xil_printf("  - TX completion did not occur.\n\r");
        }
        if (!RxDone) {
            xil_printf("  - RX packet reception did not occur.\n\r");
        }
        // Add specific error handling if needed
    } else {
        if (TxDone) {
            xil_printf("AXI DMA TX transaction complete.\n\r");
        }
        if (RxDone) {
            xil_printf("AXI DMA RX packet received.\n\r");
            // The actual packet processing happened in the ISR in this example
        }
    }

    // Disable AXI DMA interrupts before exiting
    DisableAxiDmaInterrupts(IntcInstancePtr);

    xil_printf("--- Exiting Main ---\n\r");
    return (Error ? XST_FAILURE : XST_SUCCESS);
}
