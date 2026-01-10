/**
 * @file mdns.h  
 * @brief mDNS/Bonjour service advertisement
 * 
 * Advertises the ZapLinkCore service on the local network using
 * Avahi (Linux) for zero-configuration discovery by clients.
 */

#ifndef MDNS_H
#define MDNS_H

/**
 * Initialize mDNS advertising for the ZapLinkCore service
 * Starts a background thread to handle Avahi events
 * @param port The port the HTTP server is listening on
 * @return 0 on success, non-zero on error
 */
int mdns_init(int port);

/**
 * Stop mDNS advertising and clean up Avahi resources
 */
void mdns_cleanup();

#endif
