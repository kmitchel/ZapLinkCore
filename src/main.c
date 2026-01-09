#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include "config.h"
#include "channels.h"
#include "db.h"
#include "tuner.h"
#include "http_server.h"
#include "epg.h"
#include "mdns.h"
#include "huffman.h"
#include "scanner.h"

void print_usage(const char *progname) {
    printf("Usage: %s [-p port]\n", progname);
    printf("  -p port           Port to listen on (default: %d)\n", DEFAULT_PORT);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int opt;

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Ignore SIGPIPE to prevent crash on client disconnect
    signal(SIGPIPE, SIG_IGN);

    printf("Starting ZapLinkCore...\n");
    printf("Port: %d\n", port);
    printf("Channels Config: %s\n", channels_conf_path);
    fflush(stdout);


    // -1. Check for channels config / Run Wizard
    if (scanner_check(channels_conf_path)) {
        return 0; // Scanner finished and user should verify/restart
    }

    // 0. Initialize Huffman
    huffman_init();

    // 1. Initialize DB
    if (!db_init()) {
        fprintf(stderr, "Failed to initialize database.\n");
        return 1;
    }

    // 2. Load Channels
    int count = load_channels(channels_conf_path);
    if (count <= 0) {
        // Warning only, maybe scan helper hasn't run
        fprintf(stderr, "Warning: No channels loaded from %s (or file missing).\n", channels_conf_path);
    } else {
        printf("Loaded %d channels.\n", count);
    }

    // 3. Discover Tuners
    discover_tuners();

    // 4. Start EPG Thread (Conditional Logic)
    if (db_has_data()) {
        printf("[MAIN] Database has existing data. Starting server immediately.\n");
        epg_skip_first = 1;
        start_epg_thread();
    } else {
        printf("[MAIN] Database is empty. Waiting for initial EPG scan before starting server.\n");
        epg_skip_first = 0;
        start_epg_thread();
        wait_for_first_epg_scan();
    }

    // 5. Start Discovery (mDNS & SSDP)
    mdns_init(port);

    // 6. Start HTTP Server
    start_http_server(port);

    // Cleanup
    mdns_cleanup();
    huffman_cleanup();
    db_close();
    return 0;
}
