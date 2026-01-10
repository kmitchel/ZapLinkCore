#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include "mdns.h"
#include "log.h"

// DNS constants
#define MDNS_PORT 5353
#define MDNS_ADDR "224.0.0.251"

static AvahiEntryGroup *group = NULL;
static AvahiSimplePoll *simple_poll = NULL;
static char *name = NULL;
static int service_port = 0;
static pthread_t mdns_thread;
static int should_exit = 0;
static int use_fallback = 0;

static int get_local_ip(uint32_t *addr) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return -1;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;
        *addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
        freeifaddrs(ifaddr);
        return 0;
    }
    freeifaddrs(ifaddr);
    return -1;
}

static void* mdns_fallback_worker(void *arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("[mDNS] socket failed");
        return NULL;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    struct sockaddr_in maddr;
    memset(&maddr, 0, sizeof(maddr));
    maddr.sin_family = AF_INET;
    maddr.sin_addr.s_addr = inet_addr(MDNS_ADDR);
    maddr.sin_port = htons(MDNS_PORT);

    uint32_t ip_addr = 0;
    char hostname[64];
    gethostname(hostname, sizeof(hostname));
    if (get_local_ip(&ip_addr) != 0) ip_addr = inet_addr("127.0.0.1");

    LOG_INFO("mDNS", "Fallback responder active (Port: %d, Host: %s.local, IP: %u.%u.%u.%u)", 
           service_port, hostname, 
           (ip_addr >> 0) & 0xFF, (ip_addr >> 8) & 0xFF, (ip_addr >> 16) & 0xFF, (ip_addr >> 24) & 0xFF);
    fflush(stdout);

    while (!should_exit) {
        unsigned char packet[512];
        int p = 0;

        // Header: ID=0, Flags=0x8400, Q=0, A=4
        packet[p++] = 0x00; packet[p++] = 0x00;
        packet[p++] = 0x84; packet[p++] = 0x00;
        packet[p++] = 0x00; packet[p++] = 0x00;
        packet[p++] = 0x00; packet[p++] = 0x04;
        packet[p++] = 0x00; packet[p++] = 0x00;
        packet[p++] = 0x00; packet[p++] = 0x00;

        // _http._tcp.local. (17 bytes)
        const char *svc_ptr = "\005_http\004_tcp\005local";
        // ZapLinkCore._http._tcp.local. (30 bytes)
        const char *inst_name = "\013ZapLinkCore\005_http\004_tcp\005local";
        // zaplinkcore.local. (19 bytes)
        const char *target_host = "\013zaplinkcore\005local";

        // 1. PTR: _http._tcp.local -> ZapLinkCore._http._tcp.local
        memcpy(packet+p, svc_ptr, 18); p += 18;
        packet[p++] = 0x00; packet[p++] = 0x0c; // Type: PTR
        packet[p++] = 0x00; packet[p++] = 0x01; // Class: IN
        packet[p++] = 0x00; packet[p++] = 0x00; packet[p++] = 0x01; packet[p++] = 0x2c; // TTL: 300
        packet[p++] = 0x00; packet[p++] = 30;   // RDLEN
        memcpy(packet+p, inst_name, 30); p += 30;

        // 2. SRV: ZapLinkCore._http._tcp.local -> zaplinkcore.local, port
        memcpy(packet+p, inst_name, 30); p += 30;
        packet[p++] = 0x00; packet[p++] = 0x21; // Type: SRV
        packet[p++] = 0x00; packet[p++] = 0x01; // Class: IN
        packet[p++] = 0x00; packet[p++] = 0x00; packet[p++] = 0x00; packet[p++] = 0x78; // TTL: 120
        packet[p++] = 0x00; packet[p++] = 0x19; // RDLEN: Pri(2)+Wei(2)+Port(2)+Target(19)
        packet[p++] = 0x00; packet[p++] = 0x00; // Priority
        packet[p++] = 0x00; packet[p++] = 0x00; // Weight
        packet[p++] = (service_port >> 8) & 0xFF; packet[p++] = service_port & 0xFF;
        memcpy(packet+p, target_host, 19); p += 19;

        // 3. TXT: ZapLinkCore._http._tcp.local -> "path=/playlist.m3u"
        memcpy(packet+p, inst_name, 30); p += 30;
        packet[p++] = 0x00; packet[p++] = 0x10; // Type: TXT
        packet[p++] = 0x00; packet[p++] = 0x01; // Class: IN
        packet[p++] = 0x00; packet[p++] = 0x00; packet[p++] = 0x01; packet[p++] = 0x2c; // TTL: 300
        const char *txt_data = "\022path=/playlist.m3u";
        packet[p++] = 0x00; packet[p++] = 19;   // RDLEN
        memcpy(packet+p, txt_data, 19); p += 19;

        // 4. A: zaplinkcore.local -> ip
        memcpy(packet+p, target_host, 19); p += 19;
        packet[p++] = 0x00; packet[p++] = 0x01; // Type: A
        packet[p++] = 0x00; packet[p++] = 0x01; // Class: IN
        packet[p++] = 0x00; packet[p++] = 0x00; packet[p++] = 0x00; packet[p++] = 0x78; // TTL: 120
        packet[p++] = 0x00; packet[p++] = 0x04; // RDLEN
        memcpy(packet+p, &ip_addr, 4); p += 4;

        if (sendto(sock, packet, p, 0, (struct sockaddr*)&maddr, sizeof(maddr)) < 0) {
            perror("[mDNS] sendto failed");
        }
        
        for (int i=0; i<10; i++) {
            if (should_exit) break;
            sleep(1);
        }
    }

    close(sock);
    return NULL;
}

static void create_services(AvahiClient *c);
static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata) {
    if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) LOG_INFO("mDNS", "Service '%s' established via Avahi", name);
    else if (state == AVAHI_ENTRY_GROUP_COLLISION) {
        char *n = avahi_alternative_service_name(name);
        avahi_free(name);
        name = n;
        create_services(avahi_entry_group_get_client(g));
    }
    fflush(stdout);
}

static void create_services(AvahiClient *c) {
    if (!group && !(group = avahi_entry_group_new(c, entry_group_callback, NULL))) return;
    if (avahi_entry_group_is_empty(group)) {
        avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 0, name, "_http._tcp", NULL, NULL, service_port, "path=/playlist.m3u", NULL);
        avahi_entry_group_commit(group);
    }
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void *userdata) {
    if (state == AVAHI_CLIENT_S_RUNNING) create_services(c);
}

static void* mdns_worker(void *arg) {
    (void)arg;
    while (!should_exit) {
        if (avahi_simple_poll_iterate(simple_poll, 100) < 0) break;
    }
    return NULL;
}

int mdns_init(int port) {
    AvahiClient *client = NULL;
    int error;

    service_port = port;
    name = avahi_strdup("ZapLinkCore");
    should_exit = 0;

    LOG_DEBUG("mDNS", "Initializing...");
    fflush(stdout);

    simple_poll = avahi_simple_poll_new();
    if (simple_poll) {
        client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);
        if (client) {
            LOG_DEBUG("mDNS", "Avahi client created successfully");
            fflush(stdout);
            use_fallback = 0;
            pthread_create(&mdns_thread, NULL, mdns_worker, NULL);
            return 0;
        }
        avahi_simple_poll_free(simple_poll);
        simple_poll = NULL;
    }

    LOG_DEBUG("mDNS", "Avahi daemon not available, using fallback responder");
    fflush(stdout);
    use_fallback = 1;
    pthread_create(&mdns_thread, NULL, mdns_fallback_worker, NULL);
    return 0;
}

void mdns_cleanup() {
    should_exit = 1;
    pthread_join(mdns_thread, NULL);
    if (!use_fallback && simple_poll) avahi_simple_poll_free(simple_poll);
    if (name) avahi_free(name);
}
