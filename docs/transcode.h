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
