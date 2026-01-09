#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "transcode.h"
#include "http_server.h"
#include "channels.h"
#include "tuner.h"
#include "config.h"

TranscodeBackend parse_backend(const char *str) {
    if (strcasecmp(str, "software") == 0) return BACKEND_SOFTWARE;
    if (strcasecmp(str, "qsv") == 0) return BACKEND_QSV;
    if (strcasecmp(str, "nvenc") == 0) return BACKEND_NVENC;
    if (strcasecmp(str, "vaapi") == 0) return BACKEND_VAAPI;
    return BACKEND_INVALID;
}

TranscodeCodec parse_codec(const char *str) {
    if (strcasecmp(str, "h264") == 0) return CODEC_H264;
    if (strcasecmp(str, "hevc") == 0) return CODEC_HEVC;
    if (strcasecmp(str, "av1") == 0) return CODEC_AV1;
    return CODEC_INVALID;
}

// Build FFmpeg argument list based on backend, codec, and audio settings
// Returns malloc'd argv array (caller frees)
static char **build_ffmpeg_args(TranscodeBackend backend, TranscodeCodec codec, int surround51, int *argc_out) {
    // Max 50 args for all options
    char **argv = malloc(sizeof(char*) * 50);
    int argc = 0;
    
    argv[argc++] = "ffmpeg";
    argv[argc++] = "-hide_banner";
    argv[argc++] = "-loglevel";
    argv[argc++] = "error";
    
    // Hardware acceleration input options
    switch (backend) {
        case BACKEND_QSV:
            argv[argc++] = "-hwaccel";
            argv[argc++] = "qsv";
            argv[argc++] = "-hwaccel_output_format";
            argv[argc++] = "qsv";
            break;
        case BACKEND_NVENC:
            argv[argc++] = "-hwaccel";
            argv[argc++] = "cuda";
            argv[argc++] = "-hwaccel_output_format";
            argv[argc++] = "cuda";
            break;
        case BACKEND_VAAPI:
            argv[argc++] = "-hwaccel";
            argv[argc++] = "vaapi";
            argv[argc++] = "-hwaccel_device";
            argv[argc++] = "/dev/dri/renderD128";
            argv[argc++] = "-hwaccel_output_format";
            argv[argc++] = "vaapi";
            break;
        default:
            break;
    }
    
    // Robust MPEG-TS input options for OTA streams
    argv[argc++] = "-fflags";
    argv[argc++] = "+genpts+discardcorrupt+igndts";
    argv[argc++] = "-err_detect";
    argv[argc++] = "ignore_err";
    argv[argc++] = "-probesize";
    argv[argc++] = "5M";
    argv[argc++] = "-analyzeduration";
    argv[argc++] = "5M";
    
    // Input from stdin
    argv[argc++] = "-i";
    argv[argc++] = "pipe:0";
    
    // QSV H.264 needs deinterlacing to prevent crashes (use vpp_qsv for hardware deinterlace)
    if (backend == BACKEND_QSV && codec == CODEC_H264) {
        argv[argc++] = "-vf";
        argv[argc++] = "vpp_qsv=deinterlace=2";  // 2 = bob deinterlace
    }
    
    // Video encoder
    argv[argc++] = "-c:v";
    switch (backend) {
        case BACKEND_SOFTWARE:
            switch (codec) {
                case CODEC_H264: 
                    argv[argc++] = "libx264";
                    argv[argc++] = "-preset";
                    argv[argc++] = "veryfast";
                    argv[argc++] = "-crf";
                    argv[argc++] = "23";
                    break;
                case CODEC_HEVC: 
                    argv[argc++] = "libx265";
                    argv[argc++] = "-preset";
                    argv[argc++] = "veryfast";
                    argv[argc++] = "-crf";
                    argv[argc++] = "28";
                    break;
                case CODEC_AV1:  
                    argv[argc++] = "libsvtav1";
                    argv[argc++] = "-preset";
                    argv[argc++] = "8";  // SVT-AV1 uses 0-13 (8 = fast)
                    argv[argc++] = "-crf";
                    argv[argc++] = "30";
                    break;
                default: break;
            }
            break;
            
        case BACKEND_QSV:
            switch (codec) {
                case CODEC_H264: argv[argc++] = "h264_qsv"; break;
                case CODEC_HEVC: argv[argc++] = "hevc_qsv"; break;
                case CODEC_AV1:  argv[argc++] = "av1_qsv"; break;
                default: break;
            }
            argv[argc++] = "-preset";
            argv[argc++] = "veryfast";
            break;
            
        case BACKEND_NVENC:
            switch (codec) {
                case CODEC_H264: argv[argc++] = "h264_nvenc"; break;
                case CODEC_HEVC: argv[argc++] = "hevc_nvenc"; break;
                case CODEC_AV1:  argv[argc++] = "av1_nvenc"; break;
                default: break;
            }
            argv[argc++] = "-preset";
            argv[argc++] = "p4";
            break;
            
        case BACKEND_VAAPI:
            switch (codec) {
                case CODEC_H264: argv[argc++] = "h264_vaapi"; break;
                case CODEC_HEVC: argv[argc++] = "hevc_vaapi"; break;
                case CODEC_AV1:  argv[argc++] = "av1_vaapi"; break;
                default: break;
            }
            break;
            
        default:
            break;
    }
    
    // Audio encoder and output format depend on codec
    if (codec == CODEC_AV1) {
        // AV1 uses WebM container with Opus audio
        if (surround51) {
            // 5.1 surround: remap 5.1(side) to standard 5.1 layout and use mapping_family 1
            argv[argc++] = "-af";
            argv[argc++] = "channelmap=channel_layout=5.1";
            argv[argc++] = "-c:a";
            argv[argc++] = "libopus";
            argv[argc++] = "-mapping_family";
            argv[argc++] = "1";
            argv[argc++] = "-b:a";
            argv[argc++] = "256k";
        } else {
            // Stereo downmix (default)
            argv[argc++] = "-ac";
            argv[argc++] = "2";
            argv[argc++] = "-c:a";
            argv[argc++] = "libopus";
            argv[argc++] = "-b:a";
            argv[argc++] = "128k";
        }
        argv[argc++] = "-f";
        argv[argc++] = "webm";
    } else {
        // H.264/HEVC use MPEG-TS with AAC
        if (surround51) {
            // 5.1 surround: remap 5.1(side) to standard 5.1 layout
            argv[argc++] = "-af";
            argv[argc++] = "channelmap=channel_layout=5.1";
            argv[argc++] = "-c:a";
            argv[argc++] = "aac";
            argv[argc++] = "-b:a";
            argv[argc++] = "384k";
        } else {
            // Stereo downmix (default)
            argv[argc++] = "-ac";
            argv[argc++] = "2";
            argv[argc++] = "-c:a";
            argv[argc++] = "aac";
            argv[argc++] = "-b:a";
            argv[argc++] = "128k";
        }
        argv[argc++] = "-f";
        argv[argc++] = "mpegts";
    }
    argv[argc++] = "pipe:1";
    
    argv[argc] = NULL;
    *argc_out = argc;
    return argv;
}

int handle_transcode(int sockfd, TranscodeBackend backend, TranscodeCodec codec, const char *channel, int surround51) {
    extern void send_response(int sockfd, const char *status, const char *type, const char *body);
    extern char channels_conf_path[512];
    
    // 1. Validate Channel
    Channel *c = find_channel_by_number(channel);
    if (!c) {
        send_response(sockfd, "404 Not Found", "text/plain", "Channel not found");
        return -1;
    }
    
    // 2. Acquire Tuner
    Tuner *t = acquire_tuner(USER_STREAM);
    int retries = 5;
    while (!t && retries-- > 0) {
        usleep(500000);
        t = acquire_tuner(USER_STREAM);
    }
    if (!t) {
        send_response(sockfd, "503 Service Unavailable", "text/plain", "No tuners available");
        return -1;
    }
    
    // 3. Create pipes: zap -> ffmpeg -> client
    int zap_pipe[2];   // zap stdout -> ffmpeg stdin
    int ffmpeg_pipe[2]; // ffmpeg stdout -> client
    
    if (pipe(zap_pipe) < 0 || pipe(ffmpeg_pipe) < 0) {
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Pipe creation failed");
        release_tuner(t);
        return -1;
    }
    
    // 4. Fork for dvbv5-zap
    pid_t zap_pid = fork();
    if (zap_pid == 0) {
        // Child: zap
        close(zap_pipe[0]);
        close(ffmpeg_pipe[0]);
        close(ffmpeg_pipe[1]);
        dup2(zap_pipe[1], STDOUT_FILENO);
        close(zap_pipe[1]);
        
        char adapter_id[8];
        snprintf(adapter_id, sizeof(adapter_id), "%d", t->id);
        
        execlp("dvbv5-zap", "dvbv5-zap", "-c", channels_conf_path, "-P", "-a", adapter_id, "-o", "-", c->number, NULL);
        exit(1);
    }
    
    // 5. Fork for ffmpeg
    pid_t ffmpeg_pid = fork();
    if (ffmpeg_pid == 0) {
        // Child: ffmpeg
        close(zap_pipe[1]);
        close(ffmpeg_pipe[0]);
        dup2(zap_pipe[0], STDIN_FILENO);
        dup2(ffmpeg_pipe[1], STDOUT_FILENO);
        close(zap_pipe[0]);
        close(ffmpeg_pipe[1]);
        
        int argc;
        char **argv = build_ffmpeg_args(backend, codec, surround51, &argc);
        execvp("ffmpeg", argv);
        exit(1);
    }
    
    // 6. Parent: Send headers and relay data
    t->zap_pid = zap_pid;
    close(zap_pipe[0]);
    close(zap_pipe[1]);
    close(ffmpeg_pipe[1]);
    
    // Send headers with appropriate Content-Type
    char headers[256];
    const char *content_type = (codec == CODEC_AV1) ? "video/webm" : "video/mp2t";
    snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: keep-alive\r\n"
        "\r\n", content_type);
    write(sockfd, headers, strlen(headers));
    
    char buffer[8192];
    ssize_t n;
    while ((n = read(ffmpeg_pipe[0], buffer, sizeof(buffer))) > 0) {
        if (write(sockfd, buffer, n) < 0) break;
    }
    
    // 7. Cleanup
    close(ffmpeg_pipe[0]);
    kill(ffmpeg_pid, SIGTERM);
    waitpid(ffmpeg_pid, NULL, 0);
    release_tuner(t);
    
    return 0;
}

void handle_transcode_m3u(int sockfd, const char *host, const char *backend, const char *codec) {
    extern void send_response(int sockfd, const char *status, const char *type, const char *body);
    extern Channel channels[];
    extern int channel_count;
    
    size_t cap = 1024 * 64;
    size_t size = 0;
    char *m3u = malloc(cap);
    if (!m3u) {
        send_response(sockfd, "500 Internal Server Error", "text/plain", "Memory allocation failed");
        return;
    }
    
    const char *display_host = (host && host[0] != '\0') ? host : "localhost";
    
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
        snprintf(buf, sizeof(buf), 
            "#EXTINF:-1 tvg-id=\"%s\" tvg-name=\"%s\",%s %s\n"
            "http://%s/transcode/%s/%s/%s\n",
            channels[i].number, channels[i].name, channels[i].number, channels[i].name,
            display_host, backend, codec, channels[i].number);
        APPEND_M3U(buf);
    }
    
    #undef APPEND_M3U
    
    send_response(sockfd, "200 OK", "audio/x-mpegurl", m3u);
    free(m3u);
}
