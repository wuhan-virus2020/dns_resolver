#include <ares.h>
#include <iostream>
#include <stdio.h>
#include <string.h>

#ifdef WIN32
#pragma comment(lib, "ws2_32.lib")
struct SocketInit {
    SocketInit() {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~SocketInit() {
        WSACleanup();
    }

} g_envInit;
#endif

/* Callback that is called when DNS query is finished */
static void addrinfo_cb(void *arg, int status, int timeouts,
                        struct ares_addrinfo *result) {
    (void) arg; /* Example does not use user context */
    printf("Result: %s, timeouts: %d\n", ares_strerror(status), timeouts);

    if (result) {
        struct ares_addrinfo_node *node;
        for (node = result->nodes; node != NULL; node = node->ai_next) {
            char addr_buf[64] = "";
            const void *ptr = NULL;
            if (node->ai_family == AF_INET) {
                const struct sockaddr_in *in_addr =
                        (const struct sockaddr_in *) ((void *) node->ai_addr);
                ptr = &in_addr->sin_addr;
            } else if (node->ai_family == AF_INET6) {
                const struct sockaddr_in6 *in_addr =
                        (const struct sockaddr_in6 *) ((void *) node->ai_addr);
                ptr = &in_addr->sin6_addr;
            } else {
                continue;
            }
            ares_inet_ntop(node->ai_family, ptr, addr_buf, sizeof(addr_buf));
            printf("Addr: %s\n", addr_buf);
        }
    }
    ares_freeaddrinfo(result);
}

int main(int argc, char **argv) {
    ares_channel_t *channel = NULL;
    struct ares_options options;
    int optmask = 0;
    struct ares_addrinfo_hints hints;

    if (argc != 2) {
        printf("Usage: %s domain\n", argv[0]);
        return 1;
    }

    /* Initialize library */
    ares_library_init(ARES_LIB_INIT_ALL);

    if (!ares_threadsafety()) {
        printf("c-ares not compiled with thread support\n");
        return 1;
    }

    /* Enable event thread so we don't have to monitor file descriptors */
    memset(&options, 0, sizeof(options));
    optmask |= ARES_OPT_EVENT_THREAD;
    options.evsys = ARES_EVSYS_DEFAULT;

    /* Initialize channel to run queries, a single channel can accept unlimited
   * queries */
    if (ares_init_options(&channel, &options, optmask) != ARES_SUCCESS) {
        printf("c-ares initialization issue\n");
        return 1;
    }

    /* Perform an IPv4 and IPv6 request for the provided domain name */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = ARES_AI_CANONNAME;
    ares_getaddrinfo(channel, argv[1], NULL, &hints, addrinfo_cb,
                     NULL /* user context not specified */);

    /* Wait until no more requests are left to be processed */
    ares_queue_wait_empty(channel, -1);

    /* Cleanup */
    ares_destroy(channel);

    ares_library_cleanup();
    return 0;
}