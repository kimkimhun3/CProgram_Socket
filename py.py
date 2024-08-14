import socket

# Configuration
RECEIVER_PORT = 5004  # Port where we will receive packets
DECODER_IP = "192.168.25.89"  # IP of the Decoder
DECODER_PORT = 5004  # Port of the Decoder
BUFFER_SIZE = 2048  # Size of the buffer for incoming packets

def main():
    # Create receiver socket
    receiver_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver_socket.bind(('', RECEIVER_PORT))

    # Create sender socket
    sender_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    print(f"Server started. Receiver listening on port {RECEIVER_PORT}")

    # Main loop to receive and forward packets
    while True:
        try:
            # Receive packet
            packet, sender_addr = receiver_socket.recvfrom(BUFFER_SIZE)
            print(f"Received packet from {sender_addr}")

            # Forward the packet to the decoder
            sender_socket.sendto(packet, (DECODER_IP, DECODER_PORT))
            print(f"Forwarded packet to {DECODER_IP}:{DECODER_PORT}")
        except Exception as e:
            print(f"Error: {e}")
            continue

    # Cleanup (Not reached in this infinite loop, but good practice)
    receiver_socket.close()
    sender_socket.close()

if __name__ == "__main__":
    main()
