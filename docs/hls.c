#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>

#include "hls.h"
#include "http_server.h"
#include "log.h"
#include "transcode.h"
#include "tuner.h"
#include "channels.h"

#define MAX_SESSIONS 32
#define HLS_SESSION_TIMEOUT 30 // Seconds of inactivity before cleanup

typedef struct {
    char id[64];
    char channel_num[16];
    TranscodeBackend backend;
    TranscodeCodec codec;
    int surround51;
    int bitrate_kbps;
    
    pid_t zap_pid;
    pid_t ffmpeg_pid;
    Tuner *tuner;
    
    time_t last_access;
    int active;
    
    pthread_mutex_t lock;
} HLSSession;

static HLSSession sessions[MAX_SESSIONS];
static pthread_mutex_t sessions_lock = PTHREAD_MUTEX_INITIALIZER;
static char hls_root[256] = "/tmp/zaplink_hls";

void hls_init(const char *storage_path) {
    if (storage_path) strncpy(hls_root, storage_path, sizeof(hls_root) - 1);
    
    // Create HLS root directory
    mkdir(hls_root, 0755);
    
    // Initialize session array
    memset(sessions, 0, sizeof(sessions));
    for (int i=0; i<MAX_SESSIONS; i++) {
        pthread_mutex_init(&sessions[i].lock, NULL);
    }
    
    LOG_INFO("HLS", "Initialized HLS storage at %s", hls_root);
}

void hls_cleanup() {
    pthread_mutex_lock(&sessions_lock);
    for (int i=0; i<MAX_SESSIONS; i++) {
        if (sessions[i].active) {
            // Kill processes
            if (sessions[i].ffmpeg_pid > 0) kill(sessions[i].ffmpeg_pid, SIGKILL);
            if (sessions[i].zap_pid > 0)    kill(sessions[i].zap_pid, SIGKILL);
            
            // Wait for them
            waitpid(sessions[i].ffmpeg_pid, NULL, WNOHANG);
            waitpid(sessions[i].zap_pid, NULL, WNOHANG);
            
            // Release tuner
            if (sessions[i].tuner) release_tuner(sessions[i].tuner);
            
            // Remove directory
            char path[512];
            snprintf(path, sizeof(path), "rm -rf %s/%s", hls_root, sessions[i].id);
            system(path);
            
            sessions[i].active = 0;
        }
    }
    pthread_mutex_unlock(&sessions_lock);
}

static HLSSession* find_or_create_session(TranscodeBackend backend, TranscodeCodec codec, const char *channel, int surround51, int bitrate_kbps) {
    pthread_mutex_lock(&sessions_lock);
    
    // 1. Try to find existing matching session
    for (int i=0; i<MAX_SESSIONS; i++) {
        if (sessions[i].active && 
            strcmp(sessions[i].channel_num, channel) == 0 &&
            sessions[i].backend == backend &&
            sessions[i].codec == codec &&
            sessions[i].surround51 == surround51 &&
            sessions[i].bitrate_kbps == bitrate_kbps) {
            
            sessions[i].last_access = time(NULL);
            pthread_mutex_unlock(&sessions_lock);
            return &sessions[i];
        }
    }
    
    // 2. Create new session
    for (int i=0; i<MAX_SESSIONS; i++) {
        if (!sessions[i].active) {
            HLSSession *s = &sessions[i];
            s->active = 1;
            s->backend = backend;
            s->codec = codec;
            s->surround51 = surround51;
            s->bitrate_kbps = bitrate_kbps;
            strncpy(s->channel_num, channel, sizeof(s->channel_num)-1);
            s->last_access = time(NULL);
            
            // Generate unique ID based on time
            snprintf(s->id, sizeof(s->id), "%ld_%d", time(NULL), i);
            
            pthread_mutex_unlock(&sessions_lock);
            return s;
        }
    }
    
    pthread_mutex_unlock(&sessions_lock);
    return NULL; // No free slots
}

static int start_session_processes(HLSSession *s) {
    extern char channels_conf_path[512];
    
    pthread_mutex_lock(&s->lock);
    
    if (s->ffmpeg_pid > 0) {
        // Already running
        pthread_mutex_unlock(&s->lock);
        return 0;
    }
    
    LOG_INFO("HLS", "Starting new session %s for channel %s", s->id, s->channel_num);
    
    // 1. Acquire Tuner
    Tuner *t = acquire_tuner(USER_STREAM); // Should define USER_HLS later
    if (!t) {
        LOG_ERROR("HLS", "No tuner available for session %s", s->id);
        s->active = 0; // Mark invalid
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    s->tuner = t;
    
    // 2. Create pipes
    int zap_pipe[2];
    if (pipe(zap_pipe) < 0) {
        release_tuner(t);
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    
    // 3. Create session directory
    char session_dir[512];
    snprintf(session_dir, sizeof(session_dir), "%s/%s", hls_root, s->id);
    mkdir(session_dir, 0755);
    
    // 4. Start zap
    pid_t zap_pid = fork();
    if (zap_pid == 0) {
        close(zap_pipe[0]);
        dup2(zap_pipe[1], STDOUT_FILENO);
        close(zap_pipe[1]);
        
        char adapter_id[8];
        snprintf(adapter_id, sizeof(adapter_id), "%d", t->id);
        execlp("dvbv5-zap", "dvbv5-zap", "-c", channels_conf_path, "-P", "-a", adapter_id, "-o", "-", s->channel_num, NULL);
        exit(1);
    }
    s->zap_pid = zap_pid;
    t->zap_pid = zap_pid;
    
    // 5. Start ffmpeg
    char playlist_path[768];
    snprintf(playlist_path, sizeof(playlist_path), "%s/index.m3u8", session_dir);
    
    pid_t ffmpeg_pid = fork();
    if (ffmpeg_pid == 0) {
        close(zap_pipe[1]);
        dup2(zap_pipe[0], STDIN_FILENO);
        close(zap_pipe[0]);
        
        int argc;
        char **argv = build_ffmpeg_args(s->backend, s->codec, s->surround51, s->bitrate_kbps, OUTPUT_HLS, playlist_path, &argc);
        execvp("ffmpeg", argv);
        exit(1);
    }
    s->ffmpeg_pid = ffmpeg_pid;
    
    close(zap_pipe[0]);
    close(zap_pipe[1]);
    
    pthread_mutex_unlock(&s->lock);
    
    // Wait for playlist to appear (max 10s)
    for (int i=0; i<20; i++) {
        struct stat st;
        if (stat(playlist_path, &st) == 0) break;
        usleep(500000);
    }
    
    return 0;
}

void handle_hls_playlist(int sockfd, TranscodeBackend backend, TranscodeCodec codec, const char *channel, int surround51, int bitrate_kbps) {
    extern void send_response(int sockfd, const char *status, const char *type, const char *body);
    
    // 1. Validate Channel
    Channel *c = find_channel_by_number(channel);
    if (!c) {
        send_response(sockfd, "404 Not Found", "text/plain", "Channel not found");
        return;
    }
    
    // 2. Find/Create Session
    HLSSession *s = find_or_create_session(backend, codec, channel, surround51, bitrate_kbps);
    if (!s) {
        send_response(sockfd, "503 Service Unavailable", "text/plain", "Max sessions reached");
        return;
    }
    
    // 3. Ensure processes are running
    if (s->ffmpeg_pid == 0) {
        if (start_session_processes(s) < 0) {
            send_response(sockfd, "500 Internal Server Error", "text/plain", "Failed to start HLS session");
            return;
        }
    }
    
    // 4. Read Playlist
    char playlist_path[512];
    snprintf(playlist_path, sizeof(playlist_path), "%s/%s/index.m3u8", hls_root, s->id);
    
    FILE *f = fopen(playlist_path, "rb");
    if (!f) {
        // Still starting up?
        send_response(sockfd, "503 Service Unavailable", "text/plain", "Stream initializing...");
        return;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *content = malloc(fsize + 1);
    if (content) {
        fread(content, 1, fsize, f);
        content[fsize] = 0;
        
        // Rewrite the playlist to include session ID in segment paths
        // index0.ts -> /hls/{session_id}/index0.ts
        // Simple search/replace logic or just prepend session ID since segments are relative
        // Actually, if we serve the m3u8 from /hls/{session_id}/index.m3u8, relative paths work!
        // But our endpoint is likely /hls/params.. which redirects or serves content.
        // Let's rewrite segments to absolute paths to be safe: /hls/{id}/{segment}
        
        char *rewritten = malloc(fsize * 2 + 1024); // crude size estimate
        char *ptr = rewritten;
        char *line = strtok(content, "\n");
        while (line) {
            if (strstr(line, ".ts") || strstr(line, ".m4s")) {
                ptr += sprintf(ptr, "/hls/%s/%s\n", s->id, line);
            } else {
                ptr += sprintf(ptr, "%s\n", line);
            }
            line = strtok(NULL, "\n");
        }
        
        send_response(sockfd, "200 OK", "application/vnd.apple.mpegurl", rewritten);
        
        free(rewritten);
        free(content);
    } else {
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Memory error");
    }
    
    fclose(f);
}

void handle_hls_global_playlist(int sockfd, const char *host, TranscodeBackend backend, TranscodeCodec codec, int surround51, int bitrate_kbps) {
    extern void send_response(int sockfd, const char *status, const char *type, const char *body);
    extern Channel channels[];
    extern int channel_count;
    
    // Dynamic allocation
    size_t cap = 1024 * 64;
    size_t size = 0;
    char *m3u = malloc(cap);
    if (!m3u) {
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Memory allocation failed");
        return;
    }
    
    // Helper to append
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
    
    const char *display_host = (host && host[0] != '\0') ? host : "localhost";
    const char *backend_str = "software";
    const char *codec_str = "h264";
    
    switch(backend) {
        case BACKEND_QSV: backend_str = "qsv"; break;
        case BACKEND_NVENC: backend_str = "nvenc"; break;
        case BACKEND_VAAPI: backend_str = "vaapi"; break;
        default: break;
    }
    switch(codec) {
        case CODEC_HEVC: codec_str = "hevc"; break;
        case CODEC_AV1: codec_str = "av1"; break;
        default: break;
    }
    
    // Build params string part of URL: /{backend}/{codec}/{bitrate}/{audio}
    char params[64] = {0};
    snprintf(params, sizeof(params), "/%s/%s", backend_str, codec_str);
    if (bitrate_kbps > 0) {
        snprintf(params + strlen(params), sizeof(params) - strlen(params), "/b%d", bitrate_kbps);
    }
    if (surround51) {
        strncat(params, "/ac6", sizeof(params) - strlen(params) - 1);
    }
    
    APPEND_M3U("#EXTM3U\n");
    
    for (int i = 0; i < channel_count; i++) {
        char buf[1024];
        // Generate URL: http://{host}/hls{params}/{channel}/index.m3u8
        // Note: Our handle_hls_playlist dispatch in http_server.c handles all these params flexibily.
        // We will construct: /hls/{backend}/{codec}[/bXXX][/ac6]/{channel}/index.m3u8
        
        snprintf(buf, sizeof(buf), 
            "#EXTINF:-1 tvg-id=\"%s\" tvg-name=\"%s\",%s %s\n"
            "http://%s/hls%s/%s/index.m3u8\n",
            channels[i].number, channels[i].name, channels[i].number, channels[i].name,
            display_host, params, channels[i].number);
        APPEND_M3U(buf);
    }
    
    #undef APPEND_M3U
    
    send_response(sockfd, "200 OK", "audio/x-mpegurl", m3u);
    free(m3u);
}

void handle_hls_segment(int sockfd, const char *session_id, const char *segment_file) {
    extern void send_file(int sockfd, const char *filepath, const char *content_type);
    extern void send_response(int sockfd, const char *status, const char *type, const char *body);
    
    // Security check: ensure session_id matches an active session
    // and segment_file doesn't contain ..
    if (strstr(session_id, "..") || strstr(segment_file, "..")) {
        send_response(sockfd, "403 Forbidden", "text/plain", "Invalid path");
        return;
    }
    
    // Update session last_access
    pthread_mutex_lock(&sessions_lock);
    int found = 0;
    for (int i=0; i<MAX_SESSIONS; i++) {
        if (sessions[i].active && strcmp(sessions[i].id, session_id) == 0) {
            sessions[i].last_access = time(NULL);
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&sessions_lock);
    
    if (!found) {
        send_response(sockfd, "404 Not Found", "text/plain", "Session not found");
        return;
    }
    
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/%s/%s", hls_root, session_id, segment_file);
    
    send_file(sockfd, filepath, "video/MP2T");
}

void hls_housekeeping() {
    time_t now = time(NULL);
    
    pthread_mutex_lock(&sessions_lock);
    for (int i=0; i<MAX_SESSIONS; i++) {
        if (sessions[i].active) {
            // Check if processes are alive
            int status;
            if (waitpid(sessions[i].ffmpeg_pid, &status, WNOHANG) != 0) {
                // FFmpeg died
                LOG_WARN("HLS", "Session %s FFmpeg died, cleaning up", sessions[i].id);
                sessions[i].active = 0; // Mark for next pass cleanup? 
                // Better: force timeout cleanup below
                sessions[i].last_access = 0; 
                continue;
            }
            
            if (now - sessions[i].last_access > HLS_SESSION_TIMEOUT) {
                LOG_INFO("HLS", "Session %s timed out, cleaning up", sessions[i].id);
                
                // Kill processes
                if (sessions[i].ffmpeg_pid > 0) kill(sessions[i].ffmpeg_pid, SIGKILL);
                if (sessions[i].zap_pid > 0)    kill(sessions[i].zap_pid, SIGKILL);
                waitpid(sessions[i].ffmpeg_pid, NULL, 0);
                waitpid(sessions[i].zap_pid, NULL, 0);
                
                if (sessions[i].tuner) release_tuner(sessions[i].tuner);
                
                // Keep directory for a moment? No, nuke it.
                char cmd[512];
                snprintf(cmd, sizeof(cmd), "rm -rf %s/%s", hls_root, sessions[i].id);
                system(cmd);
                
                sessions[i].active = 0;
                sessions[i].ffmpeg_pid = 0;
                sessions[i].zap_pid = 0;
            }
        }
    }
    pthread_mutex_unlock(&sessions_lock);
}
