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
#define START_BUFFERING_TIME 3000  // 3 seconds (waiting time after sending buffered packets)
#define BUFFERING_DURATION 2000     // 2 seconds (duration to buffer packets)

typedef struct {
    char *data;
    int size;
} Packet;

// Shared variables
Packet packetBuffer[MAX_PACKETS];
int bufferIndex = 0;
int buffering = 0;
SOCKET receiverSocket, senderSocket;
struct sockaddr_in decoderAddr;
CRITICAL_SECTION bufferLock;  // For synchronizing access to packetBuffer and bufferIndex

void ReceiverThreadFunction() {
    clock_t startTime = clock();

    while (1) {
        struct sockaddr_in senderAddr;
        int senderAddrSize = sizeof(senderAddr);
        char buffer[BUFFER_SIZE];
        int recvLen = recvfrom(receiverSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&senderAddr, &senderAddrSize);

        if (recvLen == SOCKET_ERROR) {
            printf("recvfrom() failed: %d\n", WSAGetLastError());
            continue;
        }

        // Critical section to protect shared resources
        EnterCriticalSection(&bufferLock);

        if (!buffering) {
            // Forward packets directly to the decoder during initial normal operation
            int result = sendto(senderSocket, buffer, recvLen, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
            if (result == SOCKET_ERROR) {
                printf("sendto() failed: %d\n", WSAGetLastError());
            }

            // Check if it's time to start buffering
            if (clock() - startTime >= START_BUFFERING_TIME * CLOCKS_PER_SEC / 1000) {
                printf("Starting 2-second buffering period\n");
                buffering = 1;
                startTime = clock();  // Reset start time for buffering duration
                bufferIndex = 0;      // Reset buffer index
            }
        } else {
            // Buffer the incoming packets
            if (bufferIndex < MAX_PACKETS) {
                packetBuffer[bufferIndex].data = (char*)malloc(recvLen);
                if (packetBuffer[bufferIndex].data == NULL) {
                    printf("malloc() failed\n");
                } else {
                    memcpy(packetBuffer[bufferIndex].data, buffer, recvLen);
                    packetBuffer[bufferIndex].size = recvLen;
                    bufferIndex++;
                }
            } else {
                printf("Buffer overflow, discarding packet\n");
            }
        }

        LeaveCriticalSection(&bufferLock);
    }
}

void SenderThreadFunction() {
    while (1) {
        if (buffering) {
            Sleep(BUFFERING_DURATION); // Simulate buffering duration

            // Critical section to protect shared resources
            EnterCriticalSection(&bufferLock);

            printf("Sending buffered packets\n");

            // Send buffered packets
            for (int i = 0; i < bufferIndex; i++) {
                int result = sendto(senderSocket, packetBuffer[i].data, packetBuffer[i].size, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
                if (result == SOCKET_ERROR) {
                    printf("sendto() failed: %d\n", WSAGetLastError());
                }
                free(packetBuffer[i].data);
            }

            // Clear the buffer and reset for the next cycle
            bufferIndex = 0;
            buffering = 0;

            // Wait for START_BUFFERING_TIME before resuming normal operation
            Sleep(START_BUFFERING_TIME);
            printf("Resuming normal operation...\n");

            LeaveCriticalSection(&bufferLock);
        }

        // Sleep briefly to yield CPU and avoid busy-waiting
        Sleep(10);
    }
}

int main() {
    WSADATA wsaData;
    struct sockaddr_in receiverAddr;
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

    // Initialize critical section
    InitializeCriticalSection(&bufferLock);

    printf("Server started. Receiver listening on port %d\n", RECEIVER_PORT);

    // Create threads
    HANDLE hReceiverThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ReceiverThreadFunction, NULL, 0, NULL);
    HANDLE hSenderThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)SenderThreadFunction, NULL, 0, NULL);

    // Wait for threads to finish (they won't in this case, so this is just for demonstration)
    WaitForSingleObject(hReceiverThread, INFINITE);
    WaitForSingleObject(hSenderThread, INFINITE);

    // Cleanup
    DeleteCriticalSection(&bufferLock);
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();

    return 0;
}
