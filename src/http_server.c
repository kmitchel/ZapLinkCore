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
#include "http_server.h"
#include "config.h"
#include "channels.h"
#include "db.h"
#include "tuner.h"

void send_response(int sockfd, const char *status, const char *type, const char *body) {
    char header[1024];
    int len = strlen(body);
    snprintf(header, sizeof(header), 
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n", status, type, len);
    
    write(sockfd, header, strlen(header));
    write(sockfd, body, len);
}

void handle_m3u(int sockfd) {
    char *m3u = malloc(1024 * 64); // 64K buffer
    char *p = m3u;
    strcpy(p, "#EXTM3U\n");
    p += strlen(p);
    
    for(int i=0; i<channel_count; i++) {
        // Assume localhost for now, or trace Host header if eager
        char buf[256];
        snprintf(buf, sizeof(buf), "#EXTINF:-1 tvg-id=\"%s\" tvg-name=\"%s\",%s %s\nhttp://localhost:%d/stream/%s\n",
            channels[i].number, channels[i].name, channels[i].number, channels[i].name, DEFAULT_PORT, channels[i].number);
        strcpy(p, buf);
        p += strlen(p);
    }
    
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

void handle_stream(int sockfd, const char *channel) {
    // 1. Validate Channel
    Channel *c = find_channel_by_number(channel);
    if (!c) {
        send_response(sockfd, "404 Not Found", "text/plain", "Channel not found");
        return;
    }

    // 2. Acquire Tuner
    Tuner *t = acquire_tuner();
    int retries = 5;
    while (!t && retries-- > 0) {
        usleep(500000); // 500ms
        t = acquire_tuner();
    }
    
    if (!t) {
        send_response(sockfd, "503 Service Unavailable", "text/plain", "No tuners available");
        return;
    }

    // 3. Send Headers
    const char *headers = "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: key-alive\r\n\r\n";
    write(sockfd, headers, strlen(headers));

    // 4. Setup Pipes and Fork
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        release_tuner(t);
        return;
    }

    // Fork for zap
    pid_t pid = fork();
    if (pid == 0) {
        // Child: exec dvbv5-zap
        // Close read end
        close(pipefd[0]);
        // Redir stdout to pipe write end
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        
        // Prepare args
        // dvbv5-zap -c channels.conf -r -a <adapter_id> -o - <channel_name>
        // Note: channels.conf format usually needs channel NAME or VCHANNEL?
        // Node implementation uses channel.number (vchannel) if using -c?
        // Node: ['-c', CHANNELS_CONF, '-r', '-a', tuner.id, '-o', '-', channel.number]
        
        char adapter_id[8];
        snprintf(adapter_id, sizeof(adapter_id), "%d", t->id);
        
        fprintf(stderr, "Executing: dvbv5-zap -c %s -r -a %s -o - \"%s\"\n", CHANNELS_CONF, adapter_id, c->name);

        execlp("dvbv5-zap", "dvbv5-zap", "-c", CHANNELS_CONF, "-r", "-a", adapter_id, "-o", "-", c->name, NULL);
        perror("exec zap failed");
        exit(1);
    } else if (pid > 0) {
        // Parent
        t->zap_pid = pid;
        close(pipefd[1]); // Close write end
        
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
        
        // Cleanup
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(pipefd[0]);
        release_tuner(t);
    } else {
        perror("fork");
        release_tuner(t);
    }
}

void *client_thread(void *arg) {
    int sockfd = *(int*)arg;
    free(arg);
    
    char buffer[2048];
    ssize_t n = read(sockfd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(sockfd);
        return NULL;
    }
    buffer[n] = 0;

    // Very basic parsing
    char method[16], path[256], protocol[16];
    sscanf(buffer, "%s %s %s", method, path, protocol);
    
    printf("Request: %s %s\n", method, path);

    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/playlist.m3u") == 0) {
            handle_m3u(sockfd);
        } else if (strcmp(path, "/xmltv.xml") == 0) {
            handle_xmltv(sockfd);
        } else if (strncmp(path, "/stream/", 8) == 0) {
            char *chan = path + 8;
            handle_stream(sockfd, chan);
        } else {
            send_response(sockfd, "404 Not Found", "text/plain", "Not Found");
        }
    } else {
        send_response(sockfd, "405 Method Not Allowed", "text/plain", "Method Not Allowed");
    }

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
