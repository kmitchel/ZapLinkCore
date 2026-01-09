#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <ctype.h>
#include "http_server.h"
#include "config.h"
#include "channels.h"
#include "db.h"
#include "tuner.h"
#include "transcode.h"

// Helper to find a specific header in the HTTP request buffer
static char *find_header(const char *buffer, const char *header_name) {
    char search[128];
    snprintf(search, sizeof(search), "%s:", header_name);
    char *p = strcasestr(buffer, search);
    
    // Ensure it's the start of a line or the first line
    if (p) {
        if (p != buffer && *(p-1) != '\n' && *(p-1) != '\r') {
             // Not at start of line, try finding next occurrence
             char *next = strcasestr(p + 1, search);
             while (next) {
                 if (*(next-1) == '\n' || *(next-1) == '\r') {
                     p = next;
                     break;
                 }
                 next = strcasestr(next + 1, search);
             }
             if (!next) p = NULL;
        }
    }
    
    if (p) {
        p += strlen(header_name);
        if (*p == ':') p++; // Skip colon
        while (*p == ' ' || *p == '\t') p++;
        
        const char *end = strchr(p, '\r');
        if (!end) end = strchr(p, '\n');
        if (end) {
            int len = end - p;
            char *val = malloc(len + 1);
            memcpy(val, p, len);
            val[len] = '\0';
            return val;
        }
    }
    return NULL;
}

void send_response(int sockfd, const char *status, const char *type, const char *body) {
    char header[1024];
    int len = body ? strlen(body) : 0;
    snprintf(header, sizeof(header), 
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "\r\n", status, type, len);
    
    write(sockfd, header, strlen(header));
    if (body && len > 0) {
        write(sockfd, body, len);
    }
}

void handle_m3u(int sockfd, const char *host) {
    // Dynamic allocation with bounds checking
    size_t cap = 1024 * 64; // Start with 64K
    size_t size = 0;
    char *m3u = malloc(cap);
    if (!m3u) {
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Memory allocation failed");
        return;
    }
    
    // Use provided host or fallback to localhost
    const char *display_host = (host && host[0] != '\0') ? host : "localhost";
    
    // Helper to append safely
    #define APPEND_M3U(str) do { \
        size_t slen = strlen(str); \
        while (size + slen + 1 > cap) { \
            cap *= 2; \
            char *tmp = realloc(m3u, cap); \
            if (!tmp) { free(m3u); send_response(sockfd, "500 Internal Server Error", "text/plain", "Memory error"); return; } \
            m3u = tmp; \
        } \
        strcpy(m3u + size, str); \
        size += slen; \
    } while(0)
    
    APPEND_M3U("#EXTM3U\n");
    
    for (int i = 0; i < channel_count; i++) {
        char buf[1024];
        // If host header didn't have port, but we're on a non-standard port, 
        // strictly speaking we should probably include it, but Host usually has it.
        snprintf(buf, sizeof(buf), "#EXTINF:-1 tvg-id=\"%s\" tvg-name=\"%s\",%s %s\nhttp://%s/stream/%s\n",
            channels[i].number, channels[i].name, channels[i].number, channels[i].name, display_host, channels[i].number);
        APPEND_M3U(buf);
    }
    
    #undef APPEND_M3U
    
    send_response(sockfd, "200 OK", "audio/x-mpegurl", m3u);
    free(m3u);
}

void handle_xmltv(int sockfd) {
    char *xml = db_get_xmltv_programs();
    if (xml) {
        send_response(sockfd, "200 OK", "application/xml", xml);
        free(xml);
    } else {
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Database Error");
    }
}

void handle_json(int sockfd) {
    char *json = db_get_json_programs();
    if (json) {
        send_response(sockfd, "200 OK", "application/json", json);
        free(json);
    } else {
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Database Error");
    }
}


void handle_stream(int sockfd, const char *channel) {
    // 1. Validate Channel
    Channel *c = find_channel_by_number(channel);
    if (!c) {
        send_response(sockfd, "404 Not Found", "text/plain", "Channel not found");
        return;
    }

    // 2. Acquire Tuner for STREAM
    Tuner *t = acquire_tuner(USER_STREAM);
    int retries = 5;
    while (!t && retries-- > 0) {
        usleep(500000); // 500ms
        t = acquire_tuner(USER_STREAM);
    }
    
    if (!t) {
        send_response(sockfd, "503 Service Unavailable", "text/plain", "No tuners available");
        return;
    }

    // 3. Setup Pipes and Fork
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Pipe creation failed");
        release_tuner(t);
        return;
    }

    // Fork for zap
    pid_t pid = fork();
    if (pid == 0) {
        // Child: exec dvbv5-zap
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        char adapter_id[8];
        snprintf(adapter_id, sizeof(adapter_id), "%d", t->id);
        
        fprintf(stderr, "Executing: dvbv5-zap -c %s -P -a %s -o - \"%s\"\n", channels_conf_path, adapter_id, c->number);

        execlp("dvbv5-zap", "dvbv5-zap", "-c", channels_conf_path, "-P", "-a", adapter_id, "-o", "-", c->number, NULL);
        perror("exec zap failed");
        exit(1);
    } else if (pid > 0) {
        // Parent
        t->zap_pid = pid;
        close(pipefd[1]); // Close write end
        
        // 4. Send Headers
        const char *headers = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: video/mp2t\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        write(sockfd, headers, strlen(headers));
        
        // Loop: Read from pipe, Write to socket
        char buffer[4096];
        ssize_t n;
        while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            ssize_t sent = write(sockfd, buffer, n);
            if (sent < 0) {
                // Client disconnected
                break;
            }
        }
        
        // Cleanup - release_tuner handles process termination
        close(pipefd[0]);
        release_tuner(t);
    } else {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Fork failed");
        release_tuner(t);
    }
}

void *client_thread(void *arg) {
    int sockfd = *(int*)arg;
    free(arg);
    
    // Set read timeout to prevent indefinite blocking
    struct timeval tv;
    tv.tv_sec = 30;  // 30 second timeout
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    char buffer[4096];
    ssize_t n = read(sockfd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(sockfd);
        return NULL;
    }
    buffer[n] = 0;

    // Safe parsing with length limits
    char method[16] = {0};
    char path[256] = {0};
    char protocol[16] = {0};
    
    if (sscanf(buffer, "%15s %255s %15s", method, path, protocol) < 2) {
        send_response(sockfd, "400 Bad Request", "text/plain", "Malformed request");
        close(sockfd);
        return NULL;
    }
    
    printf("Request: %s %s\n", method, path);

    // Strip query string
    char *query = strchr(path, '?');
    if (query) *query = '\0';

    // Find Host header
    char *host = find_header(buffer, "Host");

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/playlist.m3u") == 0) {
            handle_m3u(sockfd, host);
        } else if (strcmp(path, "/xmltv.xml") == 0) {
            handle_xmltv(sockfd);
        } else if (strcmp(path, "/xmltv.json") == 0) {
            handle_json(sockfd);
        } else if (strncmp(path, "/stream/", 8) == 0) {
            char *chan = path + 8;
            handle_stream(sockfd, chan);
        } else if (strncmp(path, "/transcode/", 11) == 0) {
            // Parse /transcode/{backend}/{codec}/{channel}[/6]
            char backend_str[32] = {0};
            char codec_str[32] = {0};
            char chan_str[64] = {0};
            int surround51 = 0;
            char *p = path + 11;
            char *slash1 = strchr(p, '/');
            if (slash1) {
                strncpy(backend_str, p, slash1 - p);
                char *slash2 = strchr(slash1 + 1, '/');
                if (slash2) {
                    strncpy(codec_str, slash1 + 1, slash2 - slash1 - 1);
                    // Check for optional /6 suffix
                    char *slash3 = strchr(slash2 + 1, '/');
                    if (slash3) {
                        strncpy(chan_str, slash2 + 1, slash3 - slash2 - 1);
                        if (strcmp(slash3 + 1, "6") == 0) {
                            surround51 = 1;
                        }
                    } else {
                        strncpy(chan_str, slash2 + 1, sizeof(chan_str) - 1);
                    }
                }
            }
            TranscodeBackend backend = parse_backend(backend_str);
            TranscodeCodec codec = parse_codec(codec_str);
            if (backend == BACKEND_INVALID || codec == CODEC_INVALID || chan_str[0] == '\0') {
                send_response(sockfd, "400 Bad Request", "text/plain", "Invalid transcode parameters");
            } else {
                handle_transcode(sockfd, backend, codec, chan_str, surround51);
            }
        } else if (strncmp(path, "/playlist/", 10) == 0) {
            // Parse /playlist/{backend}/{codec}.m3u
            char backend_str[32] = {0};
            char codec_str[32] = {0};
            char *p = path + 10;
            char *slash = strchr(p, '/');
            if (slash) {
                strncpy(backend_str, p, slash - p);
                char *dot = strstr(slash + 1, ".m3u");
                if (dot) {
                    strncpy(codec_str, slash + 1, dot - slash - 1);
                }
            }
            TranscodeBackend backend = parse_backend(backend_str);
            TranscodeCodec codec = parse_codec(codec_str);
            if (backend == BACKEND_INVALID || codec == CODEC_INVALID) {
                send_response(sockfd, "400 Bad Request", "text/plain", "Invalid playlist parameters");
            } else {
                handle_transcode_m3u(sockfd, host, backend_str, codec_str);
            }
        } else {
            send_response(sockfd, "404 Not Found", "text/plain", "Not Found");
        }
    } else {
        send_response(sockfd, "405 Method Not Allowed", "text/plain", "Method Not Allowed");
    }

    if (host) free(host);
    close(sockfd);
    return NULL;
}

void start_http_server(int port) {
    int sockfd, newsockfd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }
    
    // Reuse address
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    printf("Server listening on port %d\n", port);

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("ERROR on accept");
            continue;
        }

        pthread_t t;
        int *arg = malloc(sizeof(int));
        if (!arg) {
            close(newsockfd);
            continue;
        }
        *arg = newsockfd;
        if (pthread_create(&t, NULL, client_thread, arg) != 0) {
            perror("ERROR creating thread");
            close(newsockfd);
            free(arg);
        } else {
            pthread_detach(t);
        }
    }
}
