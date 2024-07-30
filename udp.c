#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define RECEIVER_PORT 5004  // Port where we will receive packets
#define DECODER_IP "192.168.25.89"  // IP of the Decoder
#define DECODER_PORT 5004  // Port of the Decoder
#define BUFFER_SIZE 2048  // Size of the buffer for incoming packets

int main() {
    WSADATA wsaData;
    SOCKET receiverSocket, senderSocket;
    struct sockaddr_in receiverAddr, decoderAddr;
    char buffer[BUFFER_SIZE];
    int result;

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

    // Main loop to receive and forward packets
    while (1) {
        struct sockaddr_in senderAddr;
        int senderAddrSize = sizeof(senderAddr);
        int recvLen = recvfrom(receiverSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&senderAddr, &senderAddrSize);

        if (recvLen == SOCKET_ERROR) {
            printf("recvfrom() failed: %d\n", WSAGetLastError());
            continue;
        }

        // Forward the packet to the decoder
        result = sendto(senderSocket, buffer, recvLen, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
        if (result == SOCKET_ERROR) {
            printf("sendto() failed: %d\n", WSAGetLastError());
        }
    }

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();
    return 0;
}
