import socket
import struct

def send_ethernet_frame(interface, dst_mac, payload):
    """
    Sends a raw Ethernet frame and waits for an echo response.

    Args:
        interface (str): The name of the network interface (e.g., 'eth0').
        dst_mac (str): The destination MAC address (e.g., '00:11:22:33:44:55').
        payload (bytes): The payload data to send.
    """
    try:
        # Create a raw socket
        sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))

        # Bind to the interface
        sock.bind((interface, 0))

        # Get the interface index and source MAC
        interface_index = sock.getsockname()[3]
        src_mac_bytes = sock.getsockname()[4]

        # Convert MAC addresses to bytes
        dst_mac_bytes = bytes.fromhex(dst_mac.replace(':', ''))

        # Construct the Ethernet frame
        ethernet_header = struct.pack(
            '!6s6sH',
            dst_mac_bytes,
            src_mac_bytes,
            0x0FFF  # Ethertype for IPv4 (replace as needed)
        )
        frame = ethernet_header + payload

        # Send the frame
        sock.sendto(frame, (interface, 0))
        print(f"Ethernet frame sent on interface {interface}")

        # Receive the echo response
        print("Waiting for echo response...")
        response, addr = sock.recvfrom(2048)

        # Extract the Ethernet header and payload from the response
        received_header = response[:14]
        received_payload = response[14:]

        # Verify the received payload
        if received_payload == payload:
            print("Echo response received successfully:")
            print(f"  Source MAC: {addr[4].hex(':')}")
            print(f"  Payload: {received_payload.decode()}")
        else:
            print("Error: Received payload does not match the sent payload.")

    except OSError as e:
        print(f"Error sending Ethernet frame: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    interface = "enp0s31f6"  # Replace with your interface
    dst_mac = "00:18:3E:03:61:7D"  # Replace with the destination MAC
    payload = b"This is the payload data"  # Your payload

    send_ethernet_frame(interface, dst_mac, payload)