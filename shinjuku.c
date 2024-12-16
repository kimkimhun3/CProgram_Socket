#include <stdio.h>
#include <pcap.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h> // For htons, htonl
#include <time.h>
#include <iphlpapi.h>

#define PC2_IP "192.168.25.89"
#define PC2_MAC {0xd8, 0xd4, 0xe6, 0x00, 0x11, 0x07}
#define PC2_PORT 6004
#define PC1_IP "192.168.25.69"

#define BUFFER_SIZE 65535          // Max size of a single UDP packet
#define MAX_BUFFER_PACKETS 100000         // Maximum number of packets to buffer
#define START_BUFFERING_TIME 2000  // wait time
#define BUFFERING_DURATION 0    // buffering time
#pragma comment(lib, "ws2_32.lib")  //winsock2
#pragma comment(lib, "iphlpapi.lib") //for send ARP


typedef struct {
    u_char* packet;
    uint32_t packet_len;
} BufferedPacket;

// Global buffer variables
BufferedPacket packet_buffer[MAX_BUFFER_PACKETS];
int bufferIndex = 0;
int buffering = 0;
// Removed global initialization of startTime and moved to runtime in main()

// Ethernet header structure
struct eth_header {
    uint8_t dest[6];
    uint8_t source[6];
    uint16_t type;
};

// IP header structure
struct ip_header {
    uint8_t ihl : 4;
    uint8_t version : 4;
    uint8_t tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
};

// UDP header structure
struct udp_header {
    uint16_t source;
    uint16_t dest;
    uint16_t len;
    uint16_t check;
};

// Get MAC Address of Decoder
int get_mac_address(uint32_t ip_address, uint8_t* mac_address) {
    DWORD mac_len = 6;
    ULONG remote_ip = ip_address; // IP address must be in network byte order
    ULONG mac[2] = { 0 };         // Buffer for the MAC address
    DWORD result = SendARP(remote_ip, 0, mac, &mac_len);

    if (result == NO_ERROR) {
        // Copy MAC address to the output buffer
        memcpy(mac_address, mac, mac_len);
        return 0; // Success
    }
    else {
        fprintf(stderr, "Failed to resolve MAC address. Error: %lu\n", result);
        return -1; // Failure
    }
}




// Calculate checksum
uint16_t calculate_checksum(uint16_t* addr, int len) {
    uint32_t sum = 0;
    while (len > 1) {
        sum += *addr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(uint8_t*)addr;
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return (uint16_t)~sum;
}

// Packet modification function
u_char* modify_packet(
    const u_char* original_packet,
    uint32_t packet_len,
    uint32_t dest_ip,
    uint16_t dest_port,
    uint32_t src_ip,
    uint16_t src_port,
    uint8_t* dest_mac
) {
    u_char* modified_packet = malloc(packet_len);
    if (!modified_packet) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    memcpy(modified_packet, original_packet, packet_len);

    struct eth_header* eth = (struct eth_header*)modified_packet;
    struct ip_header* ip = (struct ip_header*)(modified_packet + sizeof(struct eth_header));
    struct udp_header* udp = (struct udp_header*)(modified_packet +
        sizeof(struct eth_header) + (ip->ihl * 4));

    ip->saddr = src_ip;
    ip->daddr = dest_ip;

    ip->check = 0;
    ip->check = calculate_checksum((uint16_t*)ip, ip->ihl * 4);
    // Modify Ethernet header destination MAC
    memcpy(eth->dest, dest_mac, 6);

    udp->source = src_port;
    udp->dest = dest_port;

    udp->check = 0;

    return modified_packet;
}

// Forward packet function
int forward_packet(
    pcap_t* send_handle,
    u_char* modified_packet,
    uint32_t packet_len
    //uint8_t* dest_mac
) {
    /*struct eth_header* eth = (struct eth_header*)modified_packet;*/
    //memcpy(eth->dest, dest_mac, 6);

    if (pcap_sendpacket(send_handle, modified_packet, packet_len) != 0) {
        fprintf(stderr, "Error sending modified packet: %s\n", pcap_geterr(send_handle));
        return -1;
    }

    return 0;
}

int main() {
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t* all_devices, * device;
    pcap_t* handle;
    pcap_t* send_handle;
    struct pcap_pkthdr* header;
    const u_char* data;

    uint8_t pc2_mac[6] = PC2_MAC;
    uint32_t pc2_ip = htonl((192 << 24) | (168 << 16) | (25 << 8) | 89);
    uint16_t pc2_port = htons(PC2_PORT);

    uint32_t pc1_ip = htonl((192 << 24) | (168 << 16) | (25 << 8) | 69);
    uint16_t pc1_port = htons(5004);

    if (pcap_findalldevs(&all_devices, errbuf) == -1) {
        fprintf(stderr, "Error finding devices: %s\n", errbuf);
        return 1;
    }

    int device_index = 8, i = 0;
    for (device = all_devices; device != NULL; device = device->next) {
        ++i;
        if (i == device_index) {
            break;
        }
    }

    if (!device) {
        fprintf(stderr, "Device %d not found!\n", device_index);
        pcap_freealldevs(all_devices);
        return 1;
    }

    handle = pcap_open_live(device->name, 65536, 0, 1000, errbuf);
    if (!handle) {
        fprintf(stderr, "Could not open device %s: %s\n", device->name, errbuf);
        pcap_freealldevs(all_devices);
        return 1;
    }

    send_handle = pcap_open_live(device->name, 65536, 0, 0, errbuf);
    if (!send_handle) {
        fprintf(stderr, "Could not open device for sending: %s\n", errbuf);
        pcap_close(handle);
        pcap_freealldevs(all_devices);
        return 1;
    }

    char filter_exp[] = "udp dst port 5004";
    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filter_exp, 0, PCAP_NETMASK_UNKNOWN) == -1) {
        fprintf(stderr, "Error compiling filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Error setting filter: %s\n", pcap_geterr(handle));
        return 1;
    }

    // Initialize startTime inside main() using GetTickCount64
    clock_t startTime = clock();
    LARGE_INTEGER frequency, bufferStart, bufferEnd;
    double elapsedBufferTime = 0.0;

    // Initialize the frequency counter
    QueryPerformanceFrequency(&frequency);

    int result;
    while (1) {
        result = pcap_next_ex(handle, &header, &data);
        if (result == 0) continue;

        u_char* modified_packet = modify_packet(
            data,
            header->caplen,
            pc2_ip,
            pc2_port,
            pc1_ip,
            pc1_port,
            pc2_mac
        );

        if (!buffering || BUFFERING_DURATION == 0) {
            if (modified_packet) {
                int forward_result = forward_packet(
                    send_handle,
                    modified_packet,
                    header->caplen
                    //pc2_mac
                );

                //if (forward_result == 0) {
                //    printf("Packet modified and forwarded successfully: %d bytes\n", header->len);
                //}
                //else {
                //    fprintf(stderr, "Packet forwarding failed\n");
                //}

                if (BUFFERING_DURATION == 0) {
                    continue;
                }
                //free(modified_packet);  // Free memory after forwarding
            }
            else {
                fprintf(stderr, "Packet modification failed\n");
            }

            if (clock() - startTime >= START_BUFFERING_TIME) {
                buffering = 1;
                startTime = clock();  // Reset start time
                bufferIndex = 0;
                QueryPerformanceCounter(&bufferStart); // Start tracking buffering time
            }
        }
        else {
            if (bufferIndex < MAX_BUFFER_PACKETS) {
                packet_buffer[bufferIndex].packet = modified_packet;
                packet_buffer[bufferIndex].packet_len = header->caplen;
                bufferIndex++;
            }
            else {
                printf("Buffer overflow, discarding packet\n");
            }

            if (clock() - startTime >= BUFFERING_DURATION) {
                QueryPerformanceCounter(&bufferEnd); // End tracking buffering time

                // Calculate elapsed time
                elapsedBufferTime = (double)(bufferEnd.QuadPart - bufferStart.QuadPart) * 1000.0 / frequency.QuadPart;

                printf("Buffering duration: %.2f ms\n", elapsedBufferTime);
                for (int i = 0; i < bufferIndex; i++) {
                    forward_packet(
                        send_handle,
                        packet_buffer[i].packet,
                        packet_buffer[i].packet_len
                        /*pc2_mac*/
                    );
                    free(packet_buffer[i].packet);
                    packet_buffer[i].packet = NULL;
                    //printf("Send buffer \n");
                }
                printf("Send Buffered packets \n");
                bufferIndex = 0;
                buffering = 0;
                startTime = clock();
            }
        }
    }

    if (result == -1) {
        fprintf(stderr, "Error reading packets: %s\n", pcap_geterr(handle));
    }

    pcap_close(handle);
    pcap_close(send_handle);
    pcap_freealldevs(all_devices);

    return 0;
}
