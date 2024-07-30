#include <iostream>
#include <vector>
#include <cstring>
#include <winsock2.h>
#include <windows.h>
#include <ctime>

#pragma comment(lib, "ws2_32.lib")

#define RECEIVER_PORT 5004         // Port where we will receive packets
#define DECODER_IP "192.168.25.89" // IP of the Decoder
#define DECODER_PORT 5004          // Port of the Decoder
#define BUFFER_SIZE 65535          // Max size of a single UDP packet
#define MAX_PACKETS 100000         // Maximum number of packets to buffer
#define START_BUFFERING_TIME 2000  // 3 seconds for normal operation
#define BUFFERING_DURATION 200     // 0.5 seconds for buffering

struct Packet {
    std::vector<char> data;
    int size;
};

int main() {
    WSADATA wsaData;
    SOCKET receiverSocket, senderSocket;
    sockaddr_in receiverAddr{}, decoderAddr{};
    std::vector<Packet> packetBuffer;
    packetBuffer.reserve(MAX_PACKETS);
    int bufferIndex = 0;
    bool buffering = false;
    int result;
    clock_t startTime = clock();  // Track the start time for the initial normal operation

    // Initialize Winsock
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return 1;
    }

    // Create receiver socket
    receiverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiverSocket == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Setup receiver address structure
    receiverAddr.sin_family = AF_INET;
    receiverAddr.sin_port = htons(RECEIVER_PORT);
    receiverAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind the receiver socket
    result = bind(receiverSocket, reinterpret_cast<sockaddr *>(&receiverAddr), sizeof(receiverAddr));
    if (result == SOCKET_ERROR) {
        std::cerr << "bind() failed: " << WSAGetLastError() << std::endl;
        closesocket(receiverSocket);
        WSACleanup();
        return 1;
    }

    // Create sender socket
    senderSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (senderSocket == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << std::endl;
        closesocket(receiverSocket);
        WSACleanup();
        return 1;
    }

    // Setup decoder address structure
    decoderAddr.sin_family = AF_INET;
    decoderAddr.sin_port = htons(DECODER_PORT);
    decoderAddr.sin_addr.s_addr = inet_addr(DECODER_IP);

    std::cout << "Server started. Receiver listening on port " << RECEIVER_PORT << std::endl;

    // Main loop to receive and forward packets
    while (true) {
        sockaddr_in senderAddr{};
        int senderAddrSize = sizeof(senderAddr);
        std::vector<char> buffer(BUFFER_SIZE);
        int recvLen = recvfrom(receiverSocket, buffer.data(), BUFFER_SIZE, 0, reinterpret_cast<sockaddr *>(&senderAddr), &senderAddrSize);

        if (recvLen == SOCKET_ERROR) {
            std::cerr << "recvfrom() failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        if (!buffering) {
            // Forward packets directly to the decoder during normal operation
            result = sendto(senderSocket, buffer.data(), recvLen, 0, reinterpret_cast<sockaddr *>(&decoderAddr), sizeof(decoderAddr));
            if (result == SOCKET_ERROR) {
                std::cerr << "sendto() failed: " << WSAGetLastError() << std::endl;
            }

            // Check if it's time to start buffering
            if (clock() - startTime >= START_BUFFERING_TIME * CLOCKS_PER_SEC / 1000) {
                std::cout << "Starting buffering period" << std::endl;
                buffering = true;  // Switch to buffering mode
                bufferIndex = 0; // Reset buffer index
                startTime = clock(); // Reset start time for buffering duration
            }
        } else {
            // Buffer the incoming packets
            if (bufferIndex < MAX_PACKETS) {
                Packet packet;
                packet.data = std::vector<char>(buffer.begin(), buffer.begin() + recvLen);
                packet.size = recvLen;
                packetBuffer.push_back(packet);
                bufferIndex++;
            } else {
                std::cerr << "Buffer overflow, discarding packet" << std::endl;
            }

            // Check if buffering duration is over
            if (clock() - startTime >= BUFFERING_DURATION * CLOCKS_PER_SEC / 1000) {
                // Measure the time taken to do buffering
                clock_t endBufferingTime = clock();
                double bufferingDuration = static_cast<double>(endBufferingTime - startTime) / CLOCKS_PER_SEC;
                std::cout << "Buffer Times: " << bufferingDuration * 1000 << " ms" << std::endl;

                std::cout << "Sending buffered packets" << std::endl;

                // Calculate the total size of the buffered packets
                int totalBufferSize = 0;
                for (const auto &packet : packetBuffer) {
                    totalBufferSize += packet.size;
                }
                std::cout << "Size: " << totalBufferSize << " bytes" << std::endl;
                std::cout << "Number: " << bufferIndex << " packets" << std::endl;

                clock_t sendStartTime = clock();

                // Send buffered packets
                for (const auto &packet : packetBuffer) {
                    result = sendto(senderSocket, packet.data.data(), packet.size, 0, reinterpret_cast<sockaddr *>(&decoderAddr), sizeof(decoderAddr));
                    if (result == SOCKET_ERROR) {
                        std::cerr << "sendto() failed: " << WSAGetLastError() << std::endl;
                    }
                }

                // Measure the time taken to send buffered packets
                clock_t sendEndTime = clock();
                double sendDuration = static_cast<double>(sendEndTime - sendStartTime) / CLOCKS_PER_SEC;
                std::cout << "Time send buffered packets: " << sendDuration * 1000 << " ms" << std::endl;

                // Clear the buffer
                packetBuffer.clear();
                bufferIndex = 0;

                // Switch back to normal operation
                buffering = false;
                startTime = clock();  // Reset start time for the next normal operation period
                std::cout << "---------------------------------------------" << std::endl;
            }
        }
    }

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();
    return 0;
}
