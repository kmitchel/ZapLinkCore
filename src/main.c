/**
 * @file main.c
 * @brief ZapLinkCore entry point and lifecycle management
 * 
 * ZapLinkCore is an ATSC/DVB tuner backend that provides:
 * - Raw MPEG-TS streaming via HTTP
 * - Electronic Program Guide (EPG) collection
 * - XMLTV and JSON format EPG output
 * - mDNS service advertisement for discovery
 * 
 * Startup sequence:
 * 1. Check for channels.conf (run wizard if missing)
 * 2. Initialize SQLite database
 * 3. Load channel configuration
 * 4. Discover available tuners
 * 5. Start EPG collection (waits for first scan if DB empty)
 * 6. Start mDNS advertisement
 * 7. Start HTTP server (blocks)
 * 
 * Command line options:
 *   -p <port>  HTTP server port (default: 18392)
 *   -v         Enable verbose debug logging
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "config.h"
#include "log.h"
#include "channels.h"
#include "db.h"
#include "tuner.h"
#include "http_server.h"
#include "epg.h"
#include "mdns.h"
#include "scanner.h"

// Global verbose flag
int g_verbose = 0;

void print_usage(const char *progname) {
    printf("Usage: %s [-p port] [-v]\n", progname);
    printf("  -p port           Port to listen on (default: %d)\n", DEFAULT_PORT);
    printf("  -v                Enable verbose/debug logging\n");
}

void print_banner(int port) {
    printf("\n");
    printf(COLOR_CYAN "╔═══════════════════════════════════════════╗\n");
    printf("║" COLOR_RESET "          " COLOR_GREEN " ⚡ ZapLinkCore ⚡ " COLOR_RESET "            " COLOR_CYAN "║\n");
    printf("║" COLOR_RESET "        ATSC Tuner Server v1.0            " COLOR_CYAN "║\n");
    printf("╠═══════════════════════════════════════════╣\n");
    printf("║" COLOR_RESET "  Port: " COLOR_YELLOW "%-34d" COLOR_RESET COLOR_CYAN " ║\n", port);
    printf("║" COLOR_RESET "  Mode: " COLOR_YELLOW "%-34s" COLOR_RESET COLOR_CYAN " ║\n", g_verbose ? "Verbose" : "Normal");
    printf("╚═══════════════════════════════════════════╝" COLOR_RESET "\n\n");
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    int opt;

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "p:vh")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'v':
                g_verbose = 1;
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

    print_banner(port);
    LOG_DEBUG("MAIN", "Channels config: %s", channels_conf_path);

    // -1. Check for channels config / Run Wizard
    if (scanner_check(channels_conf_path)) {
        return 0; // Scanner finished and user should verify/restart
    }

    // 1. Initialize DB
    if (!db_init()) {
        LOG_ERROR("DB", "Failed to initialize database");
        return 1;
    }
    LOG_INFO("DB", "Database initialized");

    // 2. Load Channels
    int count = load_channels(channels_conf_path);
    if (count <= 0) {
        LOG_WARN("CHANNELS", "No channels loaded from %s", channels_conf_path);
    } else {
        LOG_INFO("CHANNELS", "Loaded %d channels", count);
    }

    // 3. Discover Tuners
    discover_tuners();

    // 4. Start EPG Thread (Conditional Logic)
    if (db_has_data()) {
        LOG_INFO("EPG", "Database has data, starting server immediately");
        epg_skip_first = 1;
        start_epg_thread();
    } else {
        LOG_INFO("EPG", "Database empty, waiting for initial scan...");
        epg_skip_first = 0;
        start_epg_thread();
        wait_for_first_epg_scan();
    }

    // 5. Start Discovery (mDNS & SSDP)
    mdns_init(port);

    // 6. Start HTTP Server
    LOG_INFO("HTTP", "Server listening on port %d", port);
    start_http_server(port);

    // Cleanup
    mdns_cleanup();
    db_close();
    return 0;
}

