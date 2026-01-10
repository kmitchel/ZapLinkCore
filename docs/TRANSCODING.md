# ZapLinkCore Transcoding System Documentation

This document provides a comprehensive technical reference for the transcoding functionality originally implemented in ZapLinkCore. This documentation is intended for porting/integrating transcoding capabilities into ZapLinkWeb or other projects.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [API Endpoints](#api-endpoints)
4. [Data Types & Enumerations](#data-types--enumerations)
5. [FFmpeg Argument Generation](#ffmpeg-argument-generation)
6. [Direct Pipe Transcoding](#direct-pipe-transcoding)
7. [HLS Streaming](#hls-streaming)
8. [Audio Handling](#audio-handling)
9. [HTTP Routing & URL Parsing](#http-routing--url-parsing)
10. [Session & Resource Management](#session--resource-management)
11. [Implementation Files Reference](#implementation-files-reference)
12. [Usage Examples](#usage-examples)

---

## Overview

The transcoding system provides real-time video transcoding from MPEG-TS streams captured via DVB tuners. Key features include:

- **Multiple hardware acceleration backends**: Software, Intel QSV, NVIDIA NVENC, VAAPI
- **Multiple output codecs**: H.264, HEVC, AV1
- **Flexible bitrate control**: Quality-based (CRF) or target bitrate (CBR/VBR)
- **Audio options**: Stereo downmix or 5.1 surround preservation
- **Two output modes**: Direct pipe streaming and HLS segment-based delivery

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        HTTP Server                               │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │  client_thread() - Route parsing & parameter extraction     ││
│  └─────────────────────────────────────────────────────────────┘│
│                           │                                      │
│         ┌─────────────────┼─────────────────┐                   │
│         ▼                 ▼                 ▼                   │
│  ┌────────────┐   ┌──────────────┐   ┌────────────────┐        │
│  │ /transcode │   │ /hls         │   │ /playlist      │        │
│  └─────┬──────┘   └──────┬───────┘   └────────┬───────┘        │
│        │                 │                     │                │
│        ▼                 ▼                     ▼                │
│  handle_transcode()  handle_hls_playlist()  handle_transcode_m3u│
│        │                 │                handle_hls_global_...  │
│        │                 │                                      │
│        └────────┬────────┘                                      │
│                 ▼                                                │
│        build_ffmpeg_args()                                       │
│                 │                                                │
│    ┌────────────┼────────────┐                                  │
│    ▼            ▼            ▼                                  │
│ OUTPUT_PIPE  OUTPUT_HLS   (Playlist                             │
│    │            │          Generation)                          │
│    ▼            ▼                                               │
│ ┌──────────────────────────────────────┐                        │
│ │  Process Pipeline (fork/exec)        │                        │
│ │  ┌──────────┐    ┌──────────┐        │                        │
│ │  │dvbv5-zap │───▶│  ffmpeg  │───▶ client/HLS                  │
│ │  └──────────┘    └──────────┘        │                        │
│ └──────────────────────────────────────┘                        │
└─────────────────────────────────────────────────────────────────┘
```

---

## API Endpoints

### Direct Transcode Streaming
**Endpoint**: `/transcode/{params...}`

Parameters can be provided in **any order**:

| Parameter | Values | Default | Description |
|-----------|--------|---------|-------------|
| backend | `software`, `qsv`, `nvenc`, `vaapi` | `software` | Encoding backend |
| codec | `h264`, `hevc`, `av1` | `h264` | Video codec |
| channel | e.g., `5.1`, `7.2` | *required* | Channel number |
| bitrate | `b{kbps}` e.g., `b500` | quality-based | Video bitrate |
| audio | `ac2`, `ac6` | `ac2` | Stereo or 5.1 surround |

**Examples**:
```
/transcode/55.1                           # Defaults: software/h264/stereo
/transcode/qsv/hevc/55.1/ac6              # Intel QSV, HEVC, 5.1 audio
/transcode/55.1/b500                      # 500kbps bitrate
/transcode/ac6/b1000/hevc/nvenc/55.1      # Any order works
```

**Response**: Continuous video stream with `Content-Type`:
- `video/mp2t` for H.264/HEVC (MPEG-TS container)
- `video/webm` for AV1 (WebM container)

---

### HLS Streaming
**Endpoint**: `/hls/{params...}/index.m3u8`

Same parameter format as transcode. Returns M3U8 playlist for HLS playback.

**Segment Endpoint**: `/hls/{session_id}/{segment_file}`

Session IDs contain underscore (`_`) and serve individual `.ts` segments.

---

### Playlist Generation

**Transcode Playlist**: `/playlist/{params...}.m3u`
Generates M3U playlist with transcode stream URLs for all channels.

**HLS Playlist**: `/playlist/hls/{params...}.m3u`
Generates M3U playlist with HLS URLs for all channels.

**Raw Playlist**: `/playlist.m3u`
Non-transcoded MPEG-TS passthrough URLs.

---

## Data Types & Enumerations

### TranscodeBackend

```c
typedef enum {
    BACKEND_SOFTWARE,   // libx264, libx265, libsvtav1
    BACKEND_QSV,        // Intel Quick Sync (h264_qsv, hevc_qsv, av1_qsv)
    BACKEND_NVENC,      // NVIDIA NVENC (h264_nvenc, hevc_nvenc, av1_nvenc)
    BACKEND_VAAPI,      // VA-API (h264_vaapi, hevc_vaapi, av1_vaapi)
    BACKEND_INVALID
} TranscodeBackend;
```

### TranscodeCodec

```c
typedef enum {
    CODEC_H264,
    CODEC_HEVC,
    CODEC_AV1,
    CODEC_INVALID
} TranscodeCodec;
```

### TranscodeOutput

```c
typedef enum {
    OUTPUT_PIPE,   // Direct stdout to client
    OUTPUT_HLS     // HLS segments to filesystem
} TranscodeOutput;
```

---

## FFmpeg Argument Generation

The `build_ffmpeg_args()` function dynamically constructs FFmpeg command-line arguments based on parameters.

### Function Signature

```c
char **build_ffmpeg_args(
    TranscodeBackend backend,    // Hardware acceleration type
    TranscodeCodec codec,        // Output codec
    int surround51,              // 1 = preserve 5.1, 0 = downmix to stereo
    int bitrate_kbps,            // Target bitrate (0 = quality-based)
    TranscodeOutput output_type, // PIPE or HLS
    const char *output_dest,     // HLS: path to m3u8, PIPE: NULL
    int *argc_out                // Returns argument count
);
```

### FFmpeg Argument Structure

#### 1. Base Arguments
```
ffmpeg -hide_banner -loglevel error
```

#### 2. Hardware Acceleration (Backend-specific)

**QSV (Intel)**:
```
-hwaccel qsv -hwaccel_output_format qsv
```

**NVENC (NVIDIA)**:
```
-hwaccel cuda -hwaccel_output_format cuda
```

**VAAPI**:
```
-hwaccel vaapi -hwaccel_device /dev/dri/renderD128 -hwaccel_output_format vaapi
```

#### 3. Input Configuration (MPEG-TS Robustness)
```
-fflags +genpts+discardcorrupt+igndts
-err_detect ignore_err
-probesize 5M
-analyzeduration 5M
-i pipe:0
```

#### 4. Deinterlacing (QSV + H.264 only)
```
-vf vpp_qsv=deinterlace=2
```
(Bob deinterlace to prevent crashes)

#### 5. Video Encoder Selection

| Backend | H.264 | HEVC | AV1 |
|---------|-------|------|-----|
| Software | `libx264` | `libx265` | `libsvtav1` |
| QSV | `h264_qsv` | `hevc_qsv` | `av1_qsv` |
| NVENC | `h264_nvenc` | `hevc_nvenc` | `av1_nvenc` |
| VAAPI | `h264_vaapi` | `hevc_vaapi` | `av1_vaapi` |

#### 6. Quality/Bitrate Settings

**Quality-based (bitrate=0)**:

| Codec | CRF Value |
|-------|-----------|
| H.264 | 23 |
| HEVC | 28 |
| AV1 | 30 |

**Bitrate-based**:
```
-b:v {bitrate}k -maxrate {bitrate}k -bufsize {bitrate*2}k
```

#### 7. Preset Selection

| Backend | Preset |
|---------|--------|
| Software (H.264/HEVC) | `veryfast` |
| Software (AV1) | `8` (SVT-AV1 scale 0-13) |
| QSV | `veryfast` |
| NVENC | `p4` |
| VAAPI | (none - uses defaults) |

---

## Audio Handling

### Stereo Mode (ac2, default)
```
-ac 2 -c:a aac -b:a 128k
-ac 2 -c:a libopus -b:a 128k   # For AV1/WebM
```

### 5.1 Surround Mode (ac6)
Channel remapping is required to handle ATSC's `5.1(side)` layout:

```
-af channelmap=channel_layout=5.1 -c:a aac -b:a 384k
-af channelmap=channel_layout=5.1 -c:a libopus -mapping_family 1 -b:a 256k
```

### Low-Bitrate Adjustment
For streams ≤1000kbps, audio bitrates are reduced:

| Mode | Normal | Low-Bitrate |
|------|--------|-------------|
| Stereo AAC | 128k | 64k |
| 5.1 AAC | 384k | 192k |
| Stereo Opus | 128k | 64k |
| 5.1 Opus | 256k | 128k |

---

## Direct Pipe Transcoding

### Process Flow

```c
int handle_transcode(int sockfd, TranscodeBackend backend, 
                     TranscodeCodec codec, const char *channel,
                     int surround51, int bitrate_kbps);
```

1. **Validate Channel**: Look up channel in loaded configuration
2. **Acquire Tuner**: Get exclusive access (with retry logic)
3. **Create Pipes**: `zap_pipe[2]` for zap→ffmpeg, `ffmpeg_pipe[2]` for ffmpeg→client
4. **Fork dvbv5-zap**: Output to pipe
5. **Fork ffmpeg**: Read from zap pipe, output to ffmpeg pipe
6. **Send HTTP Headers**: Content-Type based on codec
7. **Relay Data Loop**: Read from ffmpeg pipe, write to socket
8. **Cleanup**: Kill processes, release tuner

### Process Hierarchy

```
     Parent Process
          │
    fork()│
          ├────────────────────┐
          │                    │
     dvbv5-zap              fork()
     (stdout→pipe)             │
                         ┌─────┴─────┐
                         │           │
                      ffmpeg     Parent
                  (stdin←pipe)  (relay loop)
                  (stdout→pipe)
```

### dvbv5-zap Command
```bash
dvbv5-zap -c {channels.conf} -P -a {adapter_id} -o - {channel_number}
```

- `-c`: Channel configuration file
- `-P`: PES output mode
- `-a`: DVB adapter ID
- `-o -`: Output to stdout

---

## HLS Streaming

### Session Structure

```c
typedef struct {
    char id[64];                // Unique session ID (timestamp_index)
    char channel_num[16];       // Channel being streamed
    TranscodeBackend backend;
    TranscodeCodec codec;
    int surround51;
    int bitrate_kbps;
    
    pid_t zap_pid;              // dvbv5-zap process
    pid_t ffmpeg_pid;           // ffmpeg process
    Tuner *tuner;               // Acquired tuner
    
    time_t last_access;         // For timeout cleanup
    int active;
    
    pthread_mutex_t lock;
} HLSSession;
```

### Session Management

- **MAX_SESSIONS**: 32 concurrent HLS sessions
- **HLS_SESSION_TIMEOUT**: 30 seconds of inactivity
- **Storage Location**: `/tmp/zaplink_hls/{session_id}/`

### HLS FFmpeg Arguments (Additional)

```
-f hls
-hls_time 4                          # 4-second segments
-hls_list_size 6                     # 6 segments in playlist (24s window)
-hls_flags delete_segments+append_list
-hls_segment_type mpegts
{session_dir}/index.m3u8
```

### Playlist Rewriting

The m3u8 file is rewritten to use absolute paths:
```
index0.ts  →  /hls/{session_id}/index0.ts
index1.ts  →  /hls/{session_id}/index1.ts
```

### Housekeeping Thread

Runs every 10 seconds to:
1. Check if FFmpeg processes died unexpectedly
2. Clean up sessions inactive for >30 seconds
3. Kill associated processes and release tuners
4. Remove session directories

---

## HTTP Routing & URL Parsing

### Flexible Parameter Detection

Parameters are identified by content, not position:

```c
// Backend detection
if (strcasecmp(segment, "software") == 0 || 
    strcasecmp(segment, "qsv") == 0 || 
    strcasecmp(segment, "nvenc") == 0 || 
    strcasecmp(segment, "vaapi") == 0) {
    // It's a backend
}

// Codec detection
else if (strcasecmp(segment, "h264") == 0 || 
         strcasecmp(segment, "hevc") == 0 || 
         strcasecmp(segment, "av1") == 0) {
    // It's a codec
}

// Audio mode
else if (strcasecmp(segment, "ac6") == 0) {
    surround51 = 1;
}

// Bitrate (starts with 'b' followed by digits)
else if ((segment[0] == 'b' || segment[0] == 'B') && 
         segment[1] >= '0' && segment[1] <= '9') {
    bitrate_kbps = atoi(segment + 1);
}

// Otherwise: channel number
else {
    strncpy(chan_str, segment, ...);
}
```

### HLS Session vs Segment Detection

```c
// Session IDs contain underscore (e.g., "1704823456_5")
if (first_segment_contains('_')) {
    handle_hls_segment(sockfd, session_id, segment_file);
} else {
    // Parse as transcode parameters
    handle_hls_playlist(sockfd, ...);
}
```

---

## Session & Resource Management

### Tuner Acquisition

```c
Tuner *acquire_tuner(TunerUser purpose);  // USER_STREAM for transcoding
void release_tuner(Tuner *t);

typedef struct {
    int id;              // DVB adapter ID
    char path[512];      // /dev/dvb/adapterN
    int in_use;          // Lock flag
    pid_t zap_pid;       // Process using this tuner
    pid_t ffmpeg_pid;    // For HLS
    TunerUser user_type; // USER_STREAM, USER_EPG
} Tuner;
```

### Retry Logic

```c
Tuner *t = acquire_tuner(USER_STREAM);
int retries = 5;
while (!t && retries-- > 0) {
    usleep(500000);  // 500ms
    t = acquire_tuner(USER_STREAM);
}
if (!t) {
    send_response(sockfd, "503 Service Unavailable", ...);
    return -1;
}
```

### Cleanup Pattern

```c
// On disconnect or error
close(ffmpeg_pipe[0]);
kill(ffmpeg_pid, SIGTERM);
waitpid(ffmpeg_pid, NULL, 0);
release_tuner(t);
```

---

## Implementation Files Reference

### Core Transcoding

| File | Purpose |
|------|---------|
| `include/transcode.h` | Type definitions and function declarations |
| `src/transcode.c` | FFmpeg argument building, direct streaming |

### HLS Streaming

| File | Purpose |
|------|---------|
| `include/hls.h` | HLS function declarations |
| `src/hls.c` | Session management, HLS-specific logic |

### HTTP Routing

| File | Lines | Purpose |
|------|-------|---------|
| `src/http_server.c` | 303-368 | `/transcode/` handling |
| `src/http_server.c` | 369-443 | `/playlist/` handling |
| `src/http_server.c` | 444-533 | `/hls/` handling |

### Supporting Files

| File | Purpose |
|------|---------|
| `include/tuner.h` | Tuner resource definitions |
| `src/tuner.c` | Tuner discovery and management |
| `src/main.c` | HLS worker thread initialization |

---

## Usage Examples

### Direct Stream Playback (VLC)
```bash
# Basic H.264
curl http://localhost:18392/transcode/55.1 | vlc -

# Intel QSV HEVC with 5.1 audio
curl http://localhost:18392/transcode/qsv/hevc/55.1/ac6 | vlc -

# Low bitrate for mobile (500kbps)
curl http://localhost:18392/transcode/55.1/b500 | vlc -

# NVIDIA NVENC AV1
curl http://localhost:18392/transcode/nvenc/av1/55.1 | vlc -
```

### HLS Playback (Browser/Mobile)
```bash
# Get HLS playlist
curl http://localhost:18392/hls/55.1/index.m3u8

# Open in player
vlc http://localhost:18392/hls/qsv/hevc/55.1/index.m3u8
```

### Import Playlist (Jellyfin, Emby, etc.)
```bash
# All channels transcoded
curl http://localhost:18392/playlist/hevc/b2000.m3u > channels.m3u

# All channels as HLS
curl http://localhost:18392/playlist/hls/hevc/b1500.m3u > hls_channels.m3u
```

---

## Porting Considerations for ZapLinkWeb

1. **External Binary Dependency**: Requires `ffmpeg` with appropriate encoder support
2. **Process Management**: The fork/exec/pipe pattern may need adaptation
3. **Tuner Integration**: Currently coupled to `dvbv5-zap`; abstract input source
4. **Session Storage**: HLS uses `/tmp/zaplink_hls/`; consider configurable path
5. **Thread Safety**: Session array uses mutex; adapt for different concurrency models
6. **Container Mapping**: 
   - H.264/HEVC → MPEG-TS (`video/mp2t`)
   - AV1 → WebM (`video/webm`)
   - HLS always uses MPEG-TS segments

---

## Complete Source Reference

The original implementations are preserved below for detailed reference.

### transcode.h (46 lines)

```c
#ifndef TRANSCODE_H
#define TRANSCODE_H

typedef enum {
    BACKEND_SOFTWARE,
    BACKEND_QSV,
    BACKEND_NVENC,
    BACKEND_VAAPI,
    BACKEND_INVALID
} TranscodeBackend;

typedef enum {
    CODEC_H264,
    CODEC_HEVC,
    CODEC_AV1,
    CODEC_INVALID
} TranscodeCodec;

typedef enum {
    OUTPUT_PIPE,
    OUTPUT_HLS
} TranscodeOutput;

// Parse backend string to enum
TranscodeBackend parse_backend(const char *str);

// Parse codec string to enum
TranscodeCodec parse_codec(const char *str);

// Build FFmpeg argument list
// output_dest: filename for HLS, or NULL for pipe:1
char **build_ffmpeg_args(TranscodeBackend backend, TranscodeCodec codec, int surround51, int bitrate_kbps, TranscodeOutput output_type, const char *output_dest, int *argc_out);

// Handle a transcoded stream request
// surround51: if true, preserve 5.1 audio; if false, downmix to stereo
// bitrate_kbps: target video bitrate in kbps (0 = use default quality settings)
// Returns 0 on success, -1 on error (response already sent)
int handle_transcode(int sockfd, TranscodeBackend backend, TranscodeCodec codec, const char *channel, int surround51, int bitrate_kbps);

// Generate M3U playlist with transcoded stream URLs
// bitrate_kbps: target video bitrate in kbps (0 = use default quality settings)
// surround51: if true, preserve 5.1 audio; if false, downmix to stereo
void handle_transcode_m3u(int sockfd, const char *host, const char *backend, const char *codec, int bitrate_kbps, int surround51);

#endif
```

### hls.h (29 lines)

```c
#ifndef HLS_H
#define HLS_H

#include "transcode.h"

// Initialize HLS subsystem
void hls_init(const char *storage_path);

// Cleanup HLS subsystem
void hls_cleanup();

// Handle HLS playlist request
// Starts a session if needed, returns the playlist content
// /hls/{backend}/{codec}/{channel}/{bitrate}/index.m3u8
void handle_hls_playlist(int sockfd, TranscodeBackend backend, TranscodeCodec codec, const char *channel, int surround51, int bitrate_kbps);

// Handle global HLS playlist request (list of all channels)
// /playlist/hls...
void handle_hls_global_playlist(int sockfd, const char *host, TranscodeBackend backend, TranscodeCodec codec, int surround51, int bitrate_kbps);

// Handle HLS segment request
// /hls/{session_id}/{segment_file}
void handle_hls_segment(int sockfd, const char *session_id, const char *segment_file);

// Run housekeeper to cleanup old sessions
void hls_housekeeping();

#endif
```

---

*Document generated from ZapLinkCore codebase on 2026-01-10*
