#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

#define RECEIVER_PORT 5004         // Port where we will receive packets
#define DECODER_IP "192.168.25.89" // IP of the Decoder
#define DECODER_PORT 5004          // Port of the Decoder
#define BUFFER_SIZE 65535          // Max size of a single UDP packet
#define MAX_PACKETS 100000         // Maximum number of packets to buffer
#define START_BUFFERING_TIME 2000  // 3 seconds for normal operation
#define MAX_BUFFERED_PACKETS 1000  // Maximum number of packets to buffer before sending

typedef struct {
    char *data;
    int size;
} Packet;

// Function prototypes
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI BufferingThread(LPVOID lpParam);

// Global variable for the stop signal
volatile int stopBuffering = 0;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register the window class.
    const char CLASS_NAME[] = "Sample Window Class";

    WNDCLASS wc = { };

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // Create the window.
    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles.
        CLASS_NAME,                     // Window class
        "Packet Buffering Application", // Window text
        WS_OVERLAPPEDWINDOW,            // Window style

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

        NULL,       // Parent window    
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    // Run the message loop.
    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
        // Create a button to start buffering
        CreateWindow(
            "BUTTON",  // Predefined class; Unicode assumed 
            "Start Buffering",      // Button text 
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,  // Styles 
            10,         // x position 
            10,         // y position 
            120,        // Button width
            30,        // Button height
            hwnd,       // Parent window
            (HMENU) 1,       // ID
            (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
            NULL);      // Pointer not needed

        // Create a button to stop buffering
        CreateWindow(
            "BUTTON",  // Predefined class; Unicode assumed 
            "Stop Buffering",      // Button text 
            WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,  // Styles 
            150,        // x position 
            10,         // y position 
            120,        // Button width
            30,         // Button height
            hwnd,       // Parent window
            (HMENU) 2,       // ID
            (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE),
            NULL);      // Pointer not needed

        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1) {
            // Start the buffering thread
            stopBuffering = 0;
            CreateThread(NULL, 0, BufferingThread, NULL, 0, NULL);
        } else if (LOWORD(wParam) == 2) {
            // Signal to stop buffering
            stopBuffering = 1;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        // TODO: Add any drawing code that uses hdc here...
        EndPaint(hwnd, &ps);
    }
                  return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Function for packet buffering (your C program logic)
DWORD WINAPI BufferingThread(LPVOID lpParam) {
    WSADATA wsaData;
    SOCKET receiverSocket, senderSocket;
    struct sockaddr_in receiverAddr, decoderAddr;
    Packet packetBuffer[MAX_PACKETS];
    int bufferIndex = 0;
    int buffering = 0;
    int result;
    clock_t startTime = clock();  // Track the start time for the initial normal operation
    clock_t bufferingStartTime, bufferingEndTime; // Variables for buffering time measurement

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
    while (!stopBuffering) {
        struct sockaddr_in senderAddr;
        int senderAddrSize = sizeof(senderAddr);
        char buffer[BUFFER_SIZE];
        int recvLen = recvfrom(receiverSocket, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&senderAddr, &senderAddrSize);

        if (recvLen == SOCKET_ERROR) {
            printf("recvfrom() failed: %d\n", WSAGetLastError());
            continue;
        }

        if (!buffering) {
            // Forward packets directly to the decoder during normal operation
            result = sendto(senderSocket, buffer, recvLen, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
            if (result == SOCKET_ERROR) {
                printf("sendto() failed: %d\n", WSAGetLastError());
            }

            // Check if it's time to start buffering
            if (clock() - startTime >= START_BUFFERING_TIME * CLOCKS_PER_SEC / 1000) {
                printf("Starting buffering period\n");
                buffering = 1;  // Switch to buffering mode
                bufferIndex = 0; // Reset buffer index
                bufferingStartTime = clock(); // Record start time for buffering
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
            } else {
                printf("Buffer overflow, discarding packet\n");
            }

            // Check if the buffer has reached the specified number of packets
            if (bufferIndex >= MAX_BUFFERED_PACKETS) {
                printf("Buffered %d packets, sending now\n", bufferIndex);

                // Calculate the total size of the buffered packets
                int totalBufferSize = 0;
                for (int i = 0; i < bufferIndex; i++) {
                    totalBufferSize += packetBuffer[i].size;
                }
                printf("Total size of buffered packets: %d bytes\n", totalBufferSize);

                // Measure the time taken to buffer packets
                bufferingEndTime = clock();
                double bufferingDuration = (double)(bufferingEndTime - bufferingStartTime) / CLOCKS_PER_SEC;
                printf("Time taken to buffer packets: %.2f ms\n", bufferingDuration * 1000);

                clock_t sendStartTime = clock();

                // Send buffered packets
                for (int i = 0; i < bufferIndex; i++) {
                    result = sendto(senderSocket, packetBuffer[i].data, packetBuffer[i].size, 0, (struct sockaddr *)&decoderAddr, sizeof(decoderAddr));
                    if (result == SOCKET_ERROR) {
                        printf("sendto() failed: %d\n", WSAGetLastError());
                    }
                    free(packetBuffer[i].data);
                }

                // Measure the time taken to send buffered packets
                clock_t sendEndTime = clock();
                double sendDuration = (double)(sendEndTime - sendStartTime) / CLOCKS_PER_SEC;
                printf("Time taken to send buffered packets: %.2f ms\n", sendDuration * 1000);

                // Clear the buffer
                bufferIndex = 0;

                // Switch back to normal operation
                buffering = 0;
                startTime = clock();  // Reset start time for the next normal operation period
                printf("---------------------------------------------\n");
            }
        }
    }

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();
    return 0;
}
