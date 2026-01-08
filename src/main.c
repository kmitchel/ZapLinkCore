#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "config.h"
#include "channels.h"
#include "db.h"
#include "tuner.h"
#include "http_server.h"
#include "epg.h"

int main() {
    // Ignore SIGPIPE to prevent crash on client disconnect
    signal(SIGPIPE, SIG_IGN);

    printf("Starting C Tuner...\n");

    // 1. Initialize DB
    if (!db_init()) {
        fprintf(stderr, "Failed to initialize database.\n");
        return 1;
    }

    // 2. Load Channels
    int count = load_channels(CHANNELS_CONF);
    if (count <= 0) {
        // Warning only, maybe scan helper hasn't run
        fprintf(stderr, "Warning: No channels loaded from %s (or file missing).\n", CHANNELS_CONF);
    } else {
        printf("Loaded %d channels.\n", count);
    }

    // 3. Discover Tuners
    discover_tuners();

    // 4. Start EPG Thread
    start_epg_thread();

    // 5. Start HTTP Server
    // This blocks Main thread, so running loop is inside
    start_http_server(DEFAULT_PORT);

    // Cleanup (Unreachable in simple loop safely without signal handling in server loop, 
    // but OS cleans up resources on exit)
    db_close();
    return 0;
}
