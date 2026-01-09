#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "channels.h"
#include "config.h"

// Define the global channels array and count
Channel channels[MAX_CHANNELS];
int channel_count = 0;
char channels_conf_path[512] = CHANNELS_CONF;

// Safe string copy with null termination
static void safe_strcpy(char *dest, size_t dest_size, const char *src) {
    if (!dest || dest_size == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t src_len = strlen(src);
    size_t copy_len = (src_len < dest_size - 1) ? src_len : dest_size - 1;
    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
}

// Trim whitespace from both ends of a string (in-place)
static void trim(char *s) {
    if (!s || !*s) return;
    
    // Find start of non-whitespace
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    
    // All whitespace
    if (!*start) {
        s[0] = '\0';
        return;
    }
    
    // Find end of non-whitespace
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    
    // Move trimmed string to beginning
    size_t len = end - start + 1;
    if (start != s) {
        memmove(s, start, len);
    }
    s[len] = '\0';
}

// Compare channel numbers (Major.Minor) for qsort
static int compare_channels(const void *a, const void *b) {
    const Channel *cha = (const Channel *)a;
    const Channel *chb = (const Channel *)b;
    
    int a_major = 0, a_minor = 0;
    int b_major = 0, b_minor = 0;
    
    sscanf(cha->number, "%d.%d", &a_major, &a_minor);
    sscanf(chb->number, "%d.%d", &b_major, &b_minor);
    
    if (a_major != b_major) return a_major - b_major;
    return a_minor - b_minor;
}

int load_channels(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    // Reset channel count for new load
    channel_count = 0;

    char line[512];
    Channel *current = NULL;

    while (fgets(line, sizeof(line), f)) {
        // Trim whitespace
        trim(line);
        size_t len = strlen(line);
        
        // Skip empty lines and comments
        if (len == 0 || line[0] == '#' || line[0] == ';') continue;

        // Check for section header [ChannelName]
        if (line[0] == '[' && len > 2 && line[len-1] == ']') {
            // Save previous channel if valid
            if (current && current->frequency[0] != '\0' && channel_count < MAX_CHANNELS) {
                channel_count++;
            }
            if (channel_count >= MAX_CHANNELS) break;
            
            // Start new channel
            current = &channels[channel_count];
            memset(current, 0, sizeof(Channel));
            
            // Extract name between [ and ]
            line[len-1] = '\0';  // Remove ]
            safe_strcpy(current->name, sizeof(current->name), line + 1);
            
        } else if (current) {
            // Look for key=value (find first = only)
            char *eq = strchr(line, '=');
            if (eq && eq != line) {
                *eq = '\0';
                char *key = line;
                char *val = eq + 1;
                
                trim(key);
                trim(val);
                
                if (strcmp(key, "SERVICE_ID") == 0) {
                    safe_strcpy(current->service_id, sizeof(current->service_id), val);
                } else if (strcmp(key, "FREQUENCY") == 0) {
                    safe_strcpy(current->frequency, sizeof(current->frequency), val);
                } else if (strcmp(key, "VCHANNEL") == 0) {
                    safe_strcpy(current->number, sizeof(current->number), val);
                }
            }
        }
    }
    
    if (current && current->frequency[0] != '\0' && channel_count < MAX_CHANNELS) {
        channel_count++;
    }

    if (channel_count > 1) {
        qsort(channels, channel_count, sizeof(Channel), compare_channels);
    }

    fclose(f);
    return channel_count;
}

Channel *find_channel_by_number(const char *number) {
    if (!number) return NULL;
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i].number, number) == 0) {
            return &channels[i];
        }
    }
    return NULL;
}
