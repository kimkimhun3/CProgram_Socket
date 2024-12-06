#include <stdio.h>
#include <pcap.h>

int main() {
    char errbuf[PCAP_ERRBUF_SIZE];  // Buffer to hold error messages
    pcap_if_t* all_devices, * device; // List of devices
    pcap_t* handle;  // Handle to the opened device
    struct pcap_pkthdr* header; // Packet header
    const u_char* data; // Pointer to packet data
    struct bpf_program fcode; // Filter code
    u_int netmask; // Network mask

    // Step 1: Find all available devices
    if (pcap_findalldevs(&all_devices, errbuf) == -1) {
        fprintf(stderr, "Error finding devices: %s\n", errbuf);
        return 1;
    }

    printf("Available Devices:\n");
    int i = 0;
    for (device = all_devices; device != NULL; device = device->next) {
        printf("%d. %s\n", ++i, device->name);
        if (device->description) {
            printf("   Description: %s\n", device->description);
        }
        else {
            printf("   (No description available)\n");
        }
    }

    if (i < 8) {
        printf("Fewer than 8 devices found! Exiting.\n");
        pcap_freealldevs(all_devices);
        return 1;
    }

    // Step 2: Select device number 8
    i = 0;
    for (device = all_devices; device != NULL; device = device->next) {
        ++i;
        if (i == 8) {
            break;
        }
    }

    if (!device) {
        fprintf(stderr, "Device 8 not found!\n");
        pcap_freealldevs(all_devices);
        return 1;
    }

    printf("Selected Device: %s\n", device->name);

    // Step 3: Open the selected device for capturing
    handle = pcap_open_live(device->name, 65536, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "Could not open device %s: %s\n", device->name, errbuf);
        pcap_freealldevs(all_devices);
        return 1;
    }

    printf("Listening on device: %s\n", device->name);

    // Step 4: Set the filter for UDP packets on port 5004
    // Assuming class C netmask (0xffffff)
    netmask = 0xffffff;
    if (pcap_compile(handle, &fcode, "udp port 50004", 1, netmask) < 0) {
        fprintf(stderr, "Unable to compile the packet filter. Check the syntax.\n");
        pcap_freealldevs(all_devices);
        pcap_close(handle);
        return 1;
    }
    //if (pcap_compile(handle, &fcode, "udp", 1, netmask) < 0) {
    //    fprintf(stderr, "Unable to compile the packet f ilter. Check the syntax.\n");
    //    pcap_freealldevs(all_devices);
    //    pcap_close(handle);
    //    return 1;
    //}

    if (pcap_setfilter(handle, &fcode) < 0) {
        fprintf(stderr, "Error setting the filter.\n");
        pcap_freealldevs(all_devices);
        pcap_close(handle);
        return 1;
    }

    printf("Filter applied: Capturing only UDP packets on port 5004.\n");

    // Step 5: Capture packets in a loop
    int result;
    while ((result = pcap_next_ex(handle, &header, &data)) >= 0) {
        if (result == 0) {
            // Timeout occurred, continue
            continue;
        }

        // Print packet details
        printf("Packet captured: Length = %d bytes\n", header->len);
        for (int i = 0; i < header->len; ++i) {
            printf("%02x ", data[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        printf("\n\n");
    }

    if (result == -1) {
        fprintf(stderr, "Error reading packet: %s\n", pcap_geterr(handle));
    }

    // Cleanup
    pcap_close(handle);
    pcap_freealldevs(all_devices);

    return 0;
}
