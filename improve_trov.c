#include <iostream>
#include <vector>
#include <cstring>
#include <winsock2.h>
#include <windows.h>
#include <ctime>
#include <chrono>
#include <fstream>
#pragma comment(lib, "ws2_32.lib")

#define RECEIVER_PORT 14000         // Port where we will receive packets
#define DECODER_IP "192.168.25.89" // IP of the Decoder
#define DECODER_PORT 5004          // Port of the Decoder
#define BUFFER_SIZE 65535          // Max size of a single UDP packet
#define MAX_PACKETS 100000         // Maximum number of packets to buffer
#define START_BUFFERING_TIME 2000  // 3 seconds for normal operation
#define BUFFERING_DURATION 500    // 0.5 seconds for buffering


std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    char buffer[80];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&time));
    snprintf(buffer + strlen(buffer), sizeof(buffer) - strlen(buffer), ".%03lld", ms.count());

    return std::string(buffer);
}

struct Packet {
    std::vector<char> data;
    int size;
};

int main() {
    // Open the log file for writing (append mode)
    std::ofstream logFile("packet_timestamps.txt", std::ios::app);
    if (!logFile) {
        std::cerr << "Failed to open log file." << std::endl;
        return 1;
    }

    WSADATA wsaData;
    SOCKET receiverSocket, senderSocket;
    sockaddr_in receiverAddr{}, decoderAddr{};
    std::vector<Packet> packetBuffer;
    packetBuffer.reserve(MAX_PACKETS);
    int bufferIndex = 0;
    bool buffering = false;
    int result;
    clock_t startTime = clock();  // Track the start time for the initial normal operation
    clock_t lastBufferingTime = 0;     // Track the time of the last packet buffered
    clock_t startSendingTime = 0;      // Track the time when packets start sending after buffering
    clock_t endSendingTime = 0;  
    clock_t startBufferingTime = 0;
    clock_t finishedSendingTime = 0;


    // Initialize Winsock
    result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        logFile.close();
        return 1;
    }

    // Create receiver socket
    receiverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (receiverSocket == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << std::endl;
        logFile.close();
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
        logFile.close();
        WSACleanup();
        return 1;
    }

    // Create sender socket
    senderSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (senderSocket == INVALID_SOCKET) {
        std::cerr << "socket() failed: " << WSAGetLastError() << std::endl;
        closesocket(receiverSocket);
        logFile.close();
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
        //std::cout <<"Receive Length: " << recvLen << std::endl;
        if (recvLen == SOCKET_ERROR) {
            std::cerr << "recvfrom() failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        if (!buffering || BUFFERING_DURATION == 0) {
            // Forward packets directly to the decoder during normal operation
            result = sendto(senderSocket, buffer.data(), recvLen, 0, reinterpret_cast<sockaddr *>(&decoderAddr), sizeof(decoderAddr));
            if (result == SOCKET_ERROR) {
                std::cerr << "sendto() failed: " << WSAGetLastError() << std::endl;
            }
            // std::cout << "[Normal Operation Mode] Packet sent at: " << getCurrentTimestamp() << std::endl;
            logFile << "[Normal Operation Mode] Packet sent at: " << getCurrentTimestamp() << std::endl;
            // Skip buffering entirely if BUFFERING_DURATION is 0
            if (BUFFERING_DURATION == 0) {
                continue;
            }

            // Check if it's time to start buffering
            if (clock() - startTime >= START_BUFFERING_TIME * CLOCKS_PER_SEC / 1000) {

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
                lastBufferingTime = clock();
            } else {
                std::cerr << "Buffer overflow, discarding packet" << std::endl;
            }

            // Check if buffering duration is over
            if (clock() - startTime >= BUFFERING_DURATION * CLOCKS_PER_SEC / 1000) {
                std::cout << "Last time buffering: " << (double)(lastBufferingTime) / CLOCKS_PER_SEC << " seconds"<< std::endl;
                // Measure the time taken to do buffering
                clock_t endBufferingTime = clock();
                double bufferingDuration = static_cast<double>(endBufferingTime - startTime) / CLOCKS_PER_SEC;
                std::cout << "Buffer Times: " << bufferingDuration * 1000 << " ms" << std::endl;

                //Calculate the total size of the buffered packets
                int totalBufferSize = 0;
                for (const auto &packet : packetBuffer) {
                    totalBufferSize += packet.size;
                }
                std::cout << "Size: " << totalBufferSize << " bytes" << std::endl;
                std::cout << "Number: " << bufferIndex << " packets" << std::endl;

                // Send buffered packets
                startSendingTime = clock();
                printf("Start sending time: %.3f seconds\n", (double)(startSendingTime) / CLOCKS_PER_SEC);
                for (const auto &packet : packetBuffer) {
                    result = sendto(senderSocket, packet.data.data(), packet.size, 0, reinterpret_cast<sockaddr *>(&decoderAddr), sizeof(decoderAddr));
                    if (result == SOCKET_ERROR) {
                        std::cerr << "sendto() failed: " << WSAGetLastError() << std::endl;
                    }
                    logFile << "[Buffering Mode] Buffered packet sent at: " << getCurrentTimestamp() << std::endl;
                }
                endSendingTime = clock();
                double timeGap = static_cast<double>(startSendingTime - lastBufferingTime) / CLOCKS_PER_SEC;
                std::cout << "Time gap between finishing buffering and starting sending: " << timeGap * 1000 << " ms" << std::endl;
                // finishedSendingTime = clock();
                // printf("Finished sending buffered time: %.3f seconds\n", (double)(finishedSendingTime) / CLOCKS_PER_SEC);
                    
                // Measure the time taken to send buffered packets
                double sendDuration = static_cast<double>(endSendingTime - startSendingTime) / CLOCKS_PER_SEC;
                std::cout << "Time send buffered packets: " << sendDuration * 1000 << " ms" << std::endl;

                // Clear the buffer
                packetBuffer.clear();
                bufferIndex = 0;

                // Switch back to normal operation
                buffering = false;
                startTime = clock();  // Reset start time for the next normal operation period
                std::cout << "------------------------------------------------------------------------------" << std::endl;
                logFile << "------------------------------------------------------------------------------" << std::endl;
            }
        }
    }

    // Cleanup
    closesocket(receiverSocket);
    closesocket(senderSocket);
    WSACleanup();
    logFile.close(); // Close the log file
    return 0;
}
