#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define RECEIVER_PORT 5004         // Port where we will receive packets
#define BUFFER_SIZE 65535          // Max size of a single UDP packet
#define MAX_PACKETS 100000         // Maximum number of packets to buffer

typedef struct {
    char *data;
    int size;
} Packet;

Packet packetBuffer[MAX_PACKETS];
int bufferIndex = 0;
int buffering = 0;
int stopBuffering = 0;  // Flag to stop buffering
clock_t startTime = 0;
SOCKET receiverSocket, senderSocket;
struct sockaddr_in receiverAddr, decoderAddr;
HWND hWndStartButton, hWndStopButton, hWndDecoderPort, hWndDecoderIP, hWndStartBufferingTime, hWndBufferingDuration;

DWORD WINAPI BufferingThread(LPVOID lpParam);
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Default values for user inputs
char decoderIP[16] = "192.168.25.89";
int decoderPort = 5004;
int startBufferingTime = 1000;
int bufferingDuration = 500;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "BufferingApp";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        "BufferingApp",
        "Packet Buffering Application",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateWindow("STATIC", "Decoder IP:", WS_VISIBLE | WS_CHILD, 10, 10, 100, 20, hwnd, NULL, NULL, NULL);
            hWndDecoderIP = CreateWindow("EDIT", decoderIP, WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT, 120, 10, 150, 20, hwnd, NULL, NULL, NULL);

            CreateWindow("STATIC", "Decoder Port:", WS_VISIBLE | WS_CHILD, 10, 40, 100, 20, hwnd, NULL, NULL, NULL);
            hWndDecoderPort = CreateWindow("EDIT", "5004", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT, 120, 40, 150, 20, hwnd, NULL, NULL, NULL);

            CreateWindow("STATIC", "Start Buffering Time (ms):", WS_VISIBLE | WS_CHILD, 10, 70, 150, 20, hwnd, NULL, NULL, NULL);
            hWndStartBufferingTime = CreateWindow("EDIT", "1000", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT, 170, 70, 100, 20, hwnd, NULL, NULL, NULL);

            CreateWindow("STATIC", "Buffering Duration (ms):", WS_VISIBLE | WS_CHILD, 10, 100, 150, 20, hwnd, NULL, NULL, NULL);
            hWndBufferingDuration = CreateWindow("EDIT", "500", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_LEFT, 170, 100, 100, 20, hwnd, NULL, NULL, NULL);

            hWndStartButton = CreateWindow(
                "BUTTON", "Start Buffering",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                50, 140, 130, 30,
                hwnd, (HMENU) 1, GetModuleHandle(NULL), NULL
            );

            hWndStopButton = CreateWindow(
                "BUTTON", "Stop Buffering",
                WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                200, 140, 130, 30,
                hwnd, (HMENU) 2, GetModuleHandle(NULL), NULL
            );
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == 1) {  // Start Buffering
                GetWindowText(hWndDecoderIP, decoderIP, 16);
                char portStr[6];
                GetWindowText(hWndDecoderPort, portStr, 6);
                decoderPort = atoi(portStr);
                char startTimeStr[6];
                GetWindowText(hWndStartBufferingTime, startTimeStr, 6);
                startBufferingTime = atoi(startTimeStr) + 1000;
                char bufferingDurationStr[6];
                GetWindowText(hWndBufferingDuration, bufferingDurationStr, 6);
                bufferingDuration = atoi(bufferingDurationStr);

                stopBuffering = 0;
                CreateThread(NULL, 0, BufferingThread, NULL, 0, NULL);
            } else if (LOWORD(wParam) == 2) {  // Stop Buffering
                stopBuffering = 1;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

DWORD WINAPI BufferingThread(LPVOID lpParam) {
    WSADATA wsaData;
    int result;

    // Initialize Winsock
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        //printf("WSAStartup failed: %d\n", result);
        return 1;
    }

    // Create receiver socket
    receiverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiverSocket == INVALID_SOCKET) {
        //printf("socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // Setup receiver address structure
    memset(&receiverAddr, 0, sizeof(receiverAddr));
    receiverAddr.sin_family = AF_INET;
    receiverAddr.sin_port = htons(decoderPort);
    receiverAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind the receiver socket
    result = bind(receiverSocket, (struct sockaddr *)&receiverAddr, sizeof(receiverAddr));
    if (result == SOCKET_ERROR) {
        //printf("bind() failed: %d\n", WSAGetLastError());
        closesocket(receiverSocket);
        WSACleanup();
        return 1;
    }

    // Create sender socket
    senderSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (senderSocket == INVALID_SOCKET) {
        //printf("socket() failed: %d\n", WSAGetLastError());
        closesocket(receiverSocket);
        WSACleanup();
        return 1;
    }

    // Setup decoder address structure
    memset(&decoderAddr, 0, sizeof(decoderAddr));
    decoderAddr.sin_family = AF_INET;
    decoderAddr.sin_port = htons(decoderPort);
    decoderAddr.sin_addr.s_addr = inet_addr(decoderIP);

    //printf("Server started. Receiver listening on port %d\n", RECEIVER_PORT);

    // Main loop to receive and forward packets
    while (!stopBuffering) {
        struct sockaddr_in senderAddr;
        int senderAddrSize = sizeof(senderAddr);
        char buffer[BUFFER_SIZE];
        int recvLen = recvfrom(receiverSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&senderAddr, &senderAddrSize);

        if (recvLen == SOCKET_ERROR) {
            //printf("recvfrom() failed: %d\n", WSAGetLastError());
            continue;
        }

        if (!buffering) {
            // Forward packets directly to the decoder during normal operation
            result = sendto(senderSocket, buffer, recvLen, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
            if (result == SOCKET_ERROR) {
                //printf("sendto() failed: %d\n", WSAGetLastError());
            }

            // Check if it's time to start buffering
            if (clock() - startTime >= startBufferingTime * CLOCKS_PER_SEC / 1000) {
                buffering = 1;
                startTime = clock();  // Reset start time for the buffering period
                //printf("Buffering started\n");
            }
        } else {
            // Buffer packets during the buffering period
            if (bufferIndex < MAX_PACKETS) {
                packetBuffer[bufferIndex].data = (char *)malloc(recvLen);
                if (packetBuffer[bufferIndex].data == NULL) {
                    //printf("malloc() failed\n");
                    continue;
                }
                memcpy(packetBuffer[bufferIndex].data, buffer, recvLen);
                packetBuffer[bufferIndex].size = recvLen;
                bufferIndex++;
            } else {
                //printf("Buffer overflow, discarding packet\n");
            }

            // Check if buffering duration is over
            if (clock() - startTime >= bufferingDuration * CLOCKS_PER_SEC / 1000) {
                // Measure the time taken to do buffering
                clock_t endBufferingTime = clock();
                double bufferingDurationTime = (double)(endBufferingTime - startTime) / CLOCKS_PER_SEC;
                //printf("Buffer Times: %.2f ms\n", bufferingDurationTime * 1000);

                //printf("Sending buffered packets\n");

                // Calculate the total size of the buffered packets
                int totalBufferSize = 0;
                for (int i = 0; i < bufferIndex; i++) {
                    totalBufferSize += packetBuffer[i].size;
                }
                //printf("Size: %d bytes\n", totalBufferSize);
                //printf("Number: %d packets\n", bufferIndex);

                clock_t sendStartTime = clock();

                // Send buffered packets
                for (int i = 0; i < bufferIndex; i++) {
                    result = sendto(senderSocket, packetBuffer[i].data, packetBuffer[i].size, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
                    if (result == SOCKET_ERROR) {
                        //printf("sendto() failed: %d\n", WSAGetLastError());
                    }
                    free(packetBuffer[i].data);
                }

                // Measure the time taken to send buffered packets
                clock_t sendEndTime = clock();
                double sendDuration = (double)(sendEndTime - sendStartTime) / CLOCKS_PER_SEC;
                //printf("Time send buffered packets: %.2f ms\n", sendDuration * 1000);

                // Clear the buffer
                bufferIndex = 0;

                // Switch back to normal operation
                buffering = 0;
                startTime = clock();  // Reset start time for the next normal operation period
                //printf("---------------------------------------------\n");
            }
        }
    }

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();
    return 0;
}
