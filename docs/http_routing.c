/**
 * HTTP Routing Logic for Transcoding (Extracted from http_server.c)
 * 
 * This code was part of the client_thread() function in http_server.c
 * and handles the routing for /transcode/, /playlist/, and /hls/ endpoints.
 */

// === /transcode/ routing (lines 303-368) ===

else if (strncmp(path, "/transcode/", 11) == 0) {
    // Flexible parameter parsing - all parameters can be in any order
    // Detected by content:
    //   - backend: software, qsv, nvenc, vaapi
    //   - codec: h264, hevc, av1
    //   - channel: contains '.' (e.g., 5.1) or is looked up in channel list
    //   - bitrate: starts with 'b' followed by number (e.g., b500)
    //   - audio: ac2 (stereo, default) or ac6 (5.1 surround)
    char backend_str[32] = {0};
    char codec_str[32] = {0};
    char chan_str[64] = {0};
    int surround51 = 0;  // Default to stereo (ac2)
    int bitrate_kbps = 0;
    
    // Defaults
    strcpy(backend_str, "software");
    strcpy(codec_str, "h264");
    
    // Parse all segments after /transcode/
    char *seg = path + 11;
    while (seg && *seg) {
        char *next_slash = strchr(seg, '/');
        char segment[64] = {0};
        if (next_slash) {
            size_t len = next_slash - seg;
            if (len >= sizeof(segment)) len = sizeof(segment) - 1;
            strncpy(segment, seg, len);
            seg = next_slash + 1;
        } else {
            strncpy(segment, seg, sizeof(segment) - 1);
            seg = NULL;
        }
        
        if (segment[0] == '\0') continue;
        
        // Identify segment type
        if (strcasecmp(segment, "software") == 0 || 
            strcasecmp(segment, "qsv") == 0 || 
            strcasecmp(segment, "nvenc") == 0 || 
            strcasecmp(segment, "vaapi") == 0) {
            strncpy(backend_str, segment, sizeof(backend_str) - 1);
        } else if (strcasecmp(segment, "h264") == 0 || 
                   strcasecmp(segment, "hevc") == 0 || 
                   strcasecmp(segment, "av1") == 0) {
            strncpy(codec_str, segment, sizeof(codec_str) - 1);
        } else if (strcasecmp(segment, "ac6") == 0) {
            surround51 = 1;
        } else if (strcasecmp(segment, "ac2") == 0) {
            surround51 = 0;
        } else if ((segment[0] == 'b' || segment[0] == 'B') && 
                   segment[1] >= '0' && segment[1] <= '9') {
            // Bitrate with 'b' prefix (e.g., b500)
            bitrate_kbps = atoi(segment + 1);
        } else {
            // Assume it's a channel number
            strncpy(chan_str, segment, sizeof(chan_str) - 1);
        }
    }
    
    TranscodeBackend backend = parse_backend(backend_str);
    TranscodeCodec codec = parse_codec(codec_str);
    if (backend == BACKEND_INVALID || codec == CODEC_INVALID || chan_str[0] == '\0') {
        send_response(sockfd, "400 Bad Request", "text/plain", "Invalid transcode parameters (channel required)");
    } else {
        handle_transcode(sockfd, backend, codec, chan_str, surround51, bitrate_kbps);
    }
}

// === /playlist/ routing (lines 369-443) ===

else if (strncmp(path, "/playlist/", 10) == 0) {
    // Flexible parameter parsing for playlist - same as transcode
    // Format: /playlist/{params...}.m3u
    // Detected by content:
    //   - backend: software, qsv, nvenc, vaapi
    //   - codec: h264, hevc, av1
    //   - bitrate: starts with 'b' followed by number (e.g., b500)
    //   - audio: ac2 (stereo, default) or ac6 (5.1 surround)
    char backend_str[32] = {0};
    char codec_str[32] = {0};
    int bitrate_kbps = 0;
    int surround51 = 0;
    int is_hls_playlist = 0; // New flag for global HLS playlist
    
    // Defaults
    strcpy(backend_str, "software");
    strcpy(codec_str, "h264");
    
    // Strip .m3u extension if present (optional)
    char path_copy[256];
    strncpy(path_copy, path + 10, sizeof(path_copy) - 1);
    path_copy[sizeof(path_copy) - 1] = '\0';
    char *dot = strstr(path_copy, ".m3u");
    if (dot) *dot = '\0';
    
    // Parse all segments
    char *seg = path_copy;
    while (seg && *seg) {
        char *next_slash = strchr(seg, '/');
        char segment[64] = {0};
        if (next_slash) {
            size_t len = next_slash - seg;
            if (len >= sizeof(segment)) len = sizeof(segment) - 1;
            strncpy(segment, seg, len);
            seg = next_slash + 1;
        } else {
            strncpy(segment, seg, sizeof(segment) - 1);
            seg = NULL;
        }
        
        if (segment[0] == '\0') continue;
        
        // Identify segment type
        if (strcasecmp(segment, "software") == 0 || 
            strcasecmp(segment, "qsv") == 0 || 
            strcasecmp(segment, "nvenc") == 0 || 
            strcasecmp(segment, "vaapi") == 0) {
            strncpy(backend_str, segment, sizeof(backend_str) - 1);
        } else if (strcasecmp(segment, "h264") == 0 || 
                   strcasecmp(segment, "hevc") == 0 || 
                   strcasecmp(segment, "av1") == 0) {
            strncpy(codec_str, segment, sizeof(codec_str) - 1);
        } else if (strcasecmp(segment, "ac6") == 0) {
            surround51 = 1;
        } else if (strcasecmp(segment, "ac2") == 0) {
            surround51 = 0;
        } else if (strcasecmp(segment, "hls") == 0) {
            is_hls_playlist = 1;
        } else if ((segment[0] == 'b' || segment[0] == 'B') && 
                   segment[1] >= '0' && segment[1] <= '9') {
            bitrate_kbps = atoi(segment + 1);
        }
    }
    
    TranscodeBackend backend = parse_backend(backend_str);
    TranscodeCodec codec = parse_codec(codec_str);
    if (backend == BACKEND_INVALID || codec == CODEC_INVALID) {
        send_response(sockfd, "400 Bad Request", "text/plain", "Invalid playlist parameters");
    } else {
        if (is_hls_playlist) {
            handle_hls_global_playlist(sockfd, host, backend, codec, surround51, bitrate_kbps);
        } else {
            handle_transcode_m3u(sockfd, host, backend_str, codec_str, bitrate_kbps, surround51);
        }
    }
}

// === /hls/ routing (lines 444-533) ===

else if (strncmp(path, "/hls/", 5) == 0) {
    // HLS Handling
    // Check if first segment is a session ID (contains '_')
    char *p1 = path + 5;
    char *p2 = strchr(p1, '/');
    
    int is_segment_request = 0;
    if (p2) {
        // Check content of first segment
        int len = p2 - p1;
        for(int i=0; i<len; i++) {
            if (p1[i] == '_') {
                is_segment_request = 1; 
                break;
            }
        }
    } else {
        // Trailing? If no slash, maybe it's treated as params or ID?
        // HLS playlist is usually .../index.m3u8
    }
    
    if (is_segment_request) {
        // /hls/{session_id}/{segment}
        char session_id[64] = {0};
        size_t id_len = p2 - p1;
        if (id_len < sizeof(session_id)) strncpy(session_id, p1, id_len);
        char *segment = p2 + 1;
        handle_hls_segment(sockfd, session_id, segment);
    } else {
        // Flexible parameter parsing for HLS playlist start
        // /hls/{params...}/index.m3u8
        char backend_str[32] = {0};
        char codec_str[32] = {0};
        char chan_str[64] = {0};
        int surround51 = 0;
        int bitrate_kbps = 0;
        
        // Defaults
        strcpy(backend_str, "software");
        strcpy(codec_str, "h264");
        
        // Parse segments
        char *seg = path + 5;
        while (seg && *seg) {
            char *next_slash = strchr(seg, '/');
            char segment[64] = {0};
            if (next_slash) {
                size_t len = next_slash - seg;
                if (len >= sizeof(segment)) len = sizeof(segment) - 1;
                strncpy(segment, seg, len);
                seg = next_slash + 1;
            } else {
                strncpy(segment, seg, sizeof(segment) - 1);
                seg = NULL;
            }
            
            if (segment[0] == '\0') continue;
            if (strcmp(segment, "index.m3u8") == 0) continue; // Ignore filename
            
            if (strcasecmp(segment, "software") == 0 || 
                strcasecmp(segment, "qsv") == 0 || 
                strcasecmp(segment, "nvenc") == 0 || 
                strcasecmp(segment, "vaapi") == 0) {
                strncpy(backend_str, segment, sizeof(backend_str) - 1);
            } else if (strcasecmp(segment, "h264") == 0 || 
                       strcasecmp(segment, "hevc") == 0 || 
                       strcasecmp(segment, "av1") == 0) {
                strncpy(codec_str, segment, sizeof(codec_str) - 1);
            } else if (strcasecmp(segment, "ac6") == 0) {
                surround51 = 1;
            } else if (strcasecmp(segment, "ac2") == 0) {
                surround51 = 0;
            } else if ((segment[0] == 'b' || segment[0] == 'B') && 
                       segment[1] >= '0' && segment[1] <= '9') {
                bitrate_kbps = atoi(segment + 1);
            } else {
                // Assume channel
                strncpy(chan_str, segment, sizeof(chan_str) - 1);
            }
        }
        
        TranscodeBackend backend = parse_backend(backend_str);
        TranscodeCodec codec = parse_codec(codec_str);
        
        if (backend == BACKEND_INVALID || codec == CODEC_INVALID || chan_str[0] == '\0') {
            send_response(sockfd, "400 Bad Request", "text/plain", "Invalid parameters");
        } else {
            handle_hls_playlist(sockfd, backend, codec, chan_str, surround51, bitrate_kbps);
        }
    }
}
