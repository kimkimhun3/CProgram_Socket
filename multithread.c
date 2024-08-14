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
#define BUFFERING_DURATION 200    // 1 second

typedef struct {
    char *data;
    int size;
} Packet;

// Shared variables
Packet packetBuffer[MAX_PACKETS];
int bufferIndex = 0;
int buffering = 0;
clock_t startTime;
clock_t startBufferingTime;
clock_t lastBufferingTime;
HANDLE bufferMutex;

// Function for receiving packets
DWORD WINAPI receivePackets(LPVOID arg) {
    SOCKET receiverSocket = *(SOCKET *)arg;
    struct sockaddr_in senderAddr;
    int senderAddrSize = sizeof(senderAddr);
    char buffer[BUFFER_SIZE];

    while (1) {
        int recvLen = recvfrom(receiverSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&senderAddr, &senderAddrSize);
        if (recvLen == SOCKET_ERROR) {
            printf("recvfrom() failed: %d\n", WSAGetLastError());
            continue;
        }

        WaitForSingleObject(bufferMutex, INFINITE);
        if (!buffering) {
            SOCKET senderSocket = *(SOCKET *)arg;
            int result = sendto(senderSocket, buffer, recvLen, 0, (struct sockaddr *)&senderAddr, sizeof(senderAddr));
            if (result == SOCKET_ERROR) {
                printf("sendto() failed: %d\n", WSAGetLastError());
            }

            if (clock() - startTime >= START_BUFFERING_TIME * CLOCKS_PER_SEC / 1000) {
                printf("Starting 1-second buffering period\n");
                buffering = 1;
                startBufferingTime = clock();
                startTime = clock();  // Reset start time for buffering duration
                bufferIndex = 0;      // Reset buffer index
            }
        } else {
            if (bufferIndex < MAX_PACKETS) {
                packetBuffer[bufferIndex].data = malloc(recvLen);
                if (packetBuffer[bufferIndex].data == NULL) {
                    printf("malloc() failed\n");
                    ReleaseMutex(bufferMutex);
                    continue;
                }
                memcpy(packetBuffer[bufferIndex].data, buffer, recvLen);
                packetBuffer[bufferIndex].size = recvLen;
                bufferIndex++;
                lastBufferingTime = clock(); // Record the time of the last packet buffered
            } else {
                printf("Buffer overflow, discarding packet\n");
            }
        }
        ReleaseMutex(bufferMutex);
    }
    return 0;
}

// Function for sending buffered packets
DWORD WINAPI sendPackets(LPVOID arg) {
    SOCKET senderSocket = *(SOCKET *)arg;
    struct sockaddr_in decoderAddr;
    memset(&decoderAddr, 0, sizeof(decoderAddr));
    decoderAddr.sin_family = AF_INET;
    decoderAddr.sin_port = htons(DECODER_PORT);
    decoderAddr.sin_addr.s_addr = inet_addr(DECODER_IP);

    while (1) {
        if (buffering) {
            WaitForSingleObject(bufferMutex, INFINITE);
            if (clock() - startTime >= BUFFERING_DURATION * CLOCKS_PER_SEC / 1000) {
                double bufferingDuration = (double)(lastBufferingTime - startBufferingTime) / CLOCKS_PER_SEC;
                printf("Buffering took: %.3f seconds\n", bufferingDuration);
                printf("Sending buffered packets\n");

                for (int i = 0; i < bufferIndex; i++) {
                    int result = sendto(senderSocket, packetBuffer[i].data, packetBuffer[i].size, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
                    if (result == SOCKET_ERROR) {
                        printf("sendto() failed: %d\n", WSAGetLastError());
                    }
                    free(packetBuffer[i].data);
                }

                bufferIndex = 0;
                buffering = 0;
                startTime = clock();  // Reset start time for the next buffering period
                printf("_________________________________________________\n");
            }
            ReleaseMutex(bufferMutex);
        }
        Sleep(10); // Small delay to avoid busy waiting
    }
    return 0;
}

int main() {
    WSADATA wsaData;
    SOCKET receiverSocket, senderSocket;
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
    struct sockaddr_in receiverAddr;
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

    // Create mutex for buffer synchronization
    bufferMutex = CreateMutex(NULL, FALSE, NULL);

    // Create threads for receiving and sending packets
    HANDLE recvThread = CreateThread(NULL, 0, receivePackets, &receiverSocket, 0, NULL);
    HANDLE sendThread = CreateThread(NULL, 0, sendPackets, &senderSocket, 0, NULL);

    // Wait for threads to finish (they won't in this case)
    WaitForSingleObject(recvThread, INFINITE);
    WaitForSingleObject(sendThread, INFINITE);

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    CloseHandle(recvThread);
    CloseHandle(sendThread);
    CloseHandle(bufferMutex);
    WSACleanup();
    return 0;
}
