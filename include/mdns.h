#ifndef MDNS_H
#define MDNS_H

/**
 * Initializes mDNS advertising for the C Tuner service.
 * Starts a background thread to handle Avahi events.
 * @param port The port the HTTP server is listening on.
 * @return 0 on success, non-zero on error.
 */
int mdns_init(int port);

/**
 * Stops mDNS advertising and cleans up resources.
 */
void mdns_cleanup();

#endif
