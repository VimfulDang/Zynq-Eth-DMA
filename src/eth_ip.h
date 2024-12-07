

typedef struct {
    uint8_t dest_addr[6];   // Destination MAC address
    uint8_t src_addr[6];    // Source MAC address
    uint16_t frame_type;    // Ethernet frame type
} __attribute__((packed)) ethHdr;

// IP header structure
typedef struct {
    uint8_t version_ihl;    // Version and header length
    uint8_t tos;            // Type of service
    uint16_t total_length;  // Total length of packet
    uint16_t id;            // Identification
    uint16_t flags_fragment;// Flags and fragment offset
    uint8_t ttl;            // Time to live
    uint8_t protocol;       // Protocol
    uint16_t checksum;      // Header checksum
    uint32_t src_ip;        // Source IP address
    uint32_t dest_ip;       // Destination IP address
} ip_header;