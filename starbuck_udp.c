#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define RECEIVER_PORT 5004         // Port where we will receive packets
#define DECODER_IP "192.168.25.89" // IP of the Decoder
#define DECODER_PORT 5004          // Port of the Decoder
#define BUFFER_SIZE 65535          // Max size of a single UDP packet
#define MAX_PACKETS 100000         // Maximum number of packets to buffer
#define START_BUFFERING_TIME 3000  // 3 seconds
#define BUFFERING_DURATION 500    // 2 seconds

typedef struct {
    char *data;
    int size;
} Packet;

int main() {
    WSADATA wsaData;
    SOCKET receiverSocket, senderSocket;
    struct sockaddr_in receiverAddr, decoderAddr;
    Packet packetBuffer[MAX_PACKETS];
    int bufferIndex = 0;
    int buffering = 0;
    int result;
    clock_t startTime = clock();       // Track the start time for initial normal operation
    clock_t lastBufferingTime = 0;     // Track the time of the last packet buffered
    clock_t startSendingTime = 0;      // Track the time when packets start sending after buffering
    clock_t startBufferingTime = 0;    // Track the time when packets start buffering

    // Initialize Winsock
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return 1;
    }

    // Create receiver socket
    receiverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiverSocket == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Setup receiver address structure
    memset(&receiverAddr, 0, sizeof(receiverAddr));
    receiverAddr.sin_family = AF_INET;
    receiverAddr.sin_port = htons(RECEIVER_PORT);
    receiverAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind the receiver socket
    result = bind(receiverSocket, (struct sockaddr *)&receiverAddr, sizeof(receiverAddr));
    if (result == SOCKET_ERROR) {
        printf("bind() failed: %d\n", WSAGetLastError());
        closesocket(receiverSocket);
        WSACleanup();
        return 1;
    }

    // Create sender socket
    senderSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (senderSocket == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        closesocket(receiverSocket);
        WSACleanup();
        return 1;
    }

    // Setup decoder address structure
    memset(&decoderAddr, 0, sizeof(decoderAddr));
    decoderAddr.sin_family = AF_INET;
    decoderAddr.sin_port = htons(DECODER_PORT);
    decoderAddr.sin_addr.s_addr = inet_addr(DECODER_IP);

    printf("Server started. Receiver listening on port %d\n", RECEIVER_PORT);
    // We did check the two times of packets sending and the time of the packets buffering to make sure that everything is working fine.
    // Main loop to receive and forward packets
    //last but not least, we did checked the same thing that happen to 
    while (1) {
        struct sockaddr_in senderAddr;
        int senderAddrSize = sizeof(senderAddr);
        char buffer[BUFFER_SIZE];
        int recvLen = recvfrom(receiverSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&senderAddr, &senderAddrSize);

        if (recvLen == SOCKET_ERROR) {
            printf("recvfrom() failed: %d\n", WSAGetLastError());
            continue;
        }

        if (!buffering) {
            // Forward packets directly to the decoder during initial normal operation
            // At this stage, there is no buffering or something, incoming packets are sending to Decoder.
            result = sendto(senderSocket, buffer, recvLen, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
            if (result == SOCKET_ERROR) {
                printf("sendto() failed: %d\n", WSAGetLastError());
            }

            // Check if it's time to start buffering
            if (clock() - startTime >= START_BUFFERING_TIME * CLOCKS_PER_SEC / 1000) {
                printf("Starting 2-second buffering period\n");
                buffering = 1;
                startBufferingTime = clock();
                startTime = clock();  // Reset start time for buffering duration
                bufferIndex = 0;      // Reset buffer index
            }
        } else {
            // Buffer the incoming packets
            if (bufferIndex < MAX_PACKETS) {
                
                packetBuffer[bufferIndex].data = malloc(recvLen);
                if (packetBuffer[bufferIndex].data == NULL) {
                    printf("malloc() failed\n");
                    continue;
                }
                memcpy(packetBuffer[bufferIndex].data, buffer, recvLen);
                packetBuffer[bufferIndex].size = recvLen;
                bufferIndex++;
                lastBufferingTime = clock(); // Record the time of the last packet buffered, it cool
            } else {
                printf("Buffer overflow, discarding packet\n");
            }

            // Check if buffering duration is over
            if (clock() - startTime >= BUFFERING_DURATION * CLOCKS_PER_SEC / 1000) {
                double bufferingDuration = (double)(lastBufferingTime - startBufferingTime) / CLOCKS_PER_SEC;
                printf("Buffering took: %.3f seconds\n", bufferingDuration);
                // Print last buffering time
                printf("Last buffering time: %.3f seconds\n", (double)(lastBufferingTime) / CLOCKS_PER_SEC);
                printf("Sending buffered packets\n");
                startSendingTime = clock(); // Record the start time for sending packets
                printf("Start sending time: %.3f seconds\n", (double)(startSendingTime) / CLOCKS_PER_SEC);
                //We printed the differences between the time that it start to send the packts and the last time of doing buffering the packets.
                // Send buffered packets
                for (int i = 0; i < bufferIndex; i++) { //This is the place where we send the buffered packets to Decoder, before moving to the waitingstage
                    result = sendto(senderSocket, packetBuffer[i].data, packetBuffer[i].size, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
                    if (result == SOCKET_ERROR) {
                        printf("sendto() failed: %d\n", WSAGetLastError());
                    }
                    free(packetBuffer[i].data);
                }

                // Clear the buffer
                bufferIndex = 0;



                // Switch back to normal operation
                buffering = 0;
                startTime = clock();  // Reset start time for the next buffering period
                printf("Switched to normal operation\n");
            }
        }
    }

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();
    return 0;
}

//1. Receiving new packets during burst sending:
// A socket is an endpoint for sending or receiving data across a computer networks.
// This program run in a single-thread, blocking manner. This mean that while the program is busy sending the buffred packets in a burst,
// it may not immediately process new incoming packets from Encoder. 
// However, since the receiving socket(`receiverSocket`) is still open and bound, the OS networking stack will still continue to
// receive packets on this sockets.
//2. Where do the new packets staty?
// The new incoming packets will stay in the OS's socket receive buffer, which is a temporary storage area managed by the OS.
// Once the program finished sending the bufferd packets and return to the main loop, it will start processing the new packets that have
// accumulated in the OS buffer.
// If the program take too long to send the bufferd packaets, 
