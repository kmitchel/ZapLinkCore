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

// Parse backend string to enum
TranscodeBackend parse_backend(const char *str);

// Parse codec string to enum
TranscodeCodec parse_codec(const char *str);

// Handle a transcoded stream request
// surround51: if true, preserve 5.1 audio; if false, downmix to stereo
// Returns 0 on success, -1 on error (response already sent)
int handle_transcode(int sockfd, TranscodeBackend backend, TranscodeCodec codec, const char *channel, int surround51);

// Generate M3U playlist with transcoded stream URLs
void handle_transcode_m3u(int sockfd, const char *host, const char *backend, const char *codec);

#endif
