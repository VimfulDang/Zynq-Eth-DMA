#include <stdlib.h>

typedef struct {
    uint16_t htype;        // Hardware Type
    uint16_t ptype;        // Protocol Type
    uint8_t  hlen;         // Hardware Address Length
    uint8_t  plen;         // Protocol Address Length
    uint16_t oper;         // Operation (ARP request/reply)
    uint8_t  sha[6];       // Sender Hardware Address (MAC)
    uint8_t  spa[4];       // Sender Protocol Address (IP)
    uint8_t  tha[6];       // Target Hardware Address (MAC)
    uint8_t  tpa[4];       // Target Protocol Address (IP)
} __attribute__((packed)) arpPkt;