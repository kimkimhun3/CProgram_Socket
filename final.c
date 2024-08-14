#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define RECEIVER_PORT 5004  // Port where we will receive packets
#define DECODER_IP "192.168.25.89"  // IP of the Decoder
#define DECODER_PORT 5004  // Port of the Decoder
#define BUFFER_SIZE 65535  // Size of the buffer for incoming packets
#define MAX_PACKETS 10000  // Maximum number of packets to buffer
#define NORMAL_OPERATION_DURATION 3000  // Duration of normal operation in milliseconds
#define BUFFERING_DURATION 200  // Duration of buffering in milliseconds

int main() {
    WSADATA wsaData;
    SOCKET receiverSocket, senderSocket;
    struct sockaddr_in receiverAddr, decoderAddr;
    char *buffer = (char *)malloc(BUFFER_SIZE * sizeof(char));  // Buffer for incoming packets
    char **packetBuffer = (char **)malloc(MAX_PACKETS * sizeof(char *));  // Buffer to store incoming packets
    int *packetSizes = (int *)malloc(MAX_PACKETS * sizeof(int));  // Array to store packet sizes
    int bufferIndex = 0;
    int result;
    DWORD startTime;
    int buffering = 0;  // Flag to indicate whether we are in buffering mode
    u_long mode = 1;  // Non-blocking mode flag

    // Allocate memory for each packet in the buffer
    for (int i = 0; i < MAX_PACKETS; i++) {
        packetBuffer[i] = (char *)malloc(BUFFER_SIZE * sizeof(char));
    }

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

    // Set sender socket to non-blocking mode
    ioctlsocket(senderSocket, FIONBIO, &mode);

    // Setup decoder address structure
    memset(&decoderAddr, 0, sizeof(decoderAddr));
    decoderAddr.sin_family = AF_INET;
    decoderAddr.sin_port = htons(DECODER_PORT);
    decoderAddr.sin_addr.s_addr = inet_addr(DECODER_IP);

    printf("Server started. Receiver listening on port %d\n", RECEIVER_PORT);

    startTime = GetTickCount();  // Record the start time using GetTickCount

    // Main loop to receive and forward or buffer packets
    while (1) {
        struct sockaddr_in senderAddr;
        int senderAddrSize = sizeof(senderAddr);
        int recvLen = recvfrom(receiverSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&senderAddr, &senderAddrSize);

        if (recvLen == SOCKET_ERROR) {
            printf("recvfrom() failed: %d\n", WSAGetLastError());
            continue;
        }

        // Check if we are in normal operation or buffering mode
        if (buffering) {
            // Buffer the incoming packets
            if (bufferIndex < MAX_PACKETS) {
                memcpy(packetBuffer[bufferIndex], buffer, recvLen);
                packetSizes[bufferIndex] = recvLen;
                bufferIndex++;
            } else {
                printf("Packet buffer overflow!\n");
            }

            // Check if buffering duration has elapsed
            if (GetTickCount() - startTime >= BUFFERING_DURATION) {
                // Send all buffered packets to the decoder in a burst
                for (int i = 0; i < bufferIndex; i++) {
                    result = sendto(senderSocket, packetBuffer[i], packetSizes[i], 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
                    if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                        printf("sendto() failed: %d\n", WSAGetLastError());
                    }
                }
                printf("Buffered %d packets sent in a burst.\n", bufferIndex);
                bufferIndex = 0;  // Reset the buffer index
                startTime = GetTickCount();  // Reset the start time
                buffering = 0;  // Switch to normal operation
            }
        } else {
            // Forward the packet directly to the decoder
            result = sendto(senderSocket, buffer, recvLen, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
            if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                printf("sendto() failed: %d\n", WSAGetLastError());
            }

            // Check if normal operation duration has elapsed
            if (GetTickCount() - startTime >= NORMAL_OPERATION_DURATION) {
                startTime = GetTickCount();  // Reset the start time
                buffering = 1;  // Switch to buffering mode
                printf("Switching to buffering mode.\n");
            }
        }
    }

    // Cleanup
    for (int i = 0; i < MAX_PACKETS; i++) {
        free(packetBuffer[i]);  // Free memory for each packet
    }
    free(packetBuffer);
    free(packetSizes);
    free(buffer);
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();
    return 0;
}
