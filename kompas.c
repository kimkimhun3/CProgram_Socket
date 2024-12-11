#include <stdio.h>
#include <pcap.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h> // For htons, htonl
#include <time.h>

#define PC2_IP "192.168.25.89"
#define PC2_MAC {0xd8, 0xd4, 0xe6, 0x00, 0x11, 0x07}
#define PC2_PORT 5004
#define PC1_IP "192.168.25.69"

#define MAX_BUFFER_PACKETS 10000
#define BUFFER_TIME_MS 500
#define BUFFER_START_TIME 5 

typedef struct {
    u_char* packet;
    uint32_t packet_len;
} BufferedPacket;

// Global buffer variables
BufferedPacket packet_buffer[MAX_BUFFER_PACKETS];
int buffer_count = 0;
int buffering_active = 0;
clock_t buffer_start_time = 0;
clock_t program_start_time = 0;


//winsock2
#pragma comment(lib, "ws2_32.lib")

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
// Modify packet function
u_char* modify_packet(
    const u_char* original_packet,
    uint32_t packet_len,
    uint32_t dest_ip,
    uint16_t dest_port,
    uint32_t src_ip,
    uint16_t src_port
) {
    // Allocate memory for modified packet
    u_char* modified_packet = malloc(packet_len);
    if (!modified_packet) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    // Copy original packet
    memcpy(modified_packet, original_packet, packet_len);

    // Get header pointers
    struct eth_header* eth = (struct eth_header*)modified_packet;
    struct ip_header* ip = (struct ip_header*)(modified_packet + sizeof(struct eth_header));
    struct udp_header* udp = (struct udp_header*)(modified_packet +
        sizeof(struct eth_header) +
        (ip->ihl * 4));

    // Modify IP addresses
    ip->saddr = src_ip;
    ip->daddr = dest_ip;

    // Recalculate IP header checksum
    ip->check = 0;
    ip->check = calculate_checksum((uint16_t*)ip, ip->ihl * 4);

    // Modify UDP ports
    udp->source = src_port;
    udp->dest = dest_port;

    // Reset UDP checksum (optional, depending on your requirements)
    udp->check = 0;

    return modified_packet;
}

// Forward packet function
int forward_packet(
    pcap_t* send_handle,
    u_char* modified_packet,
    uint32_t packet_len,
    uint8_t* dest_mac
) {
    // Update destination MAC address
    struct eth_header* eth = (struct eth_header*)modified_packet;
    memcpy(eth->dest, dest_mac, 6);

    // Send the modified packet
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

    char filter_exp[] = "udp port 50004";
    struct bpf_program fp;
    if (pcap_compile(handle, &fp, filter_exp, 0, PCAP_NETMASK_UNKNOWN) == -1) {
        fprintf(stderr, "Error compiling filter: %s\n", pcap_geterr(handle));
        return 1;
    }
    if (pcap_setfilter(handle, &fp) == -1) {
        fprintf(stderr, "Error setting filter: %s\n", pcap_geterr(handle));
        return 1;
    }

    int result;
    while ((result = pcap_next_ex(handle, &header, &data)) >= 0) {
        if (result == 0) continue;

        // Modify packet
        u_char* modified_packet = modify_packet(
            data,
            header->caplen,
            pc2_ip,
            pc2_port,
            pc1_ip,
            pc1_port
        );

        if (modified_packet) {
            // Forward modified packet
            int forward_result = forward_packet(
                send_handle,
                modified_packet,
                header->caplen,
                pc2_mac
            );

            if (forward_result == 0) {
                printf("Packet modified and forwarded successfully: %d bytes\n", header->len);
            }
            else {
                fprintf(stderr, "Packet forwarding failed\n");
            }

            // Free the modified packet
            free(modified_packet);
        }
        else {
            fprintf(stderr, "Packet modification failed\n");
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
