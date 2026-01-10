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
