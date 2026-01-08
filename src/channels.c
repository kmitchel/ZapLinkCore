#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "channels.h"
#include "config.h"

Channel channels[MAX_CHANNELS];
int channel_count = 0;

void trim(char *s) {
    char *p = s;
    int l = strlen(p);

    while(isspace(p[l - 1])) p[--l] = 0;
    while(*p && isspace(*p)) ++p, --l;

    memmove(s, p, l + 1);
}

int load_channels(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[256];
    Channel *current = NULL;

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = line;
        // Trim leading whitespace
        while(*trimmed && (*trimmed == ' ' || *trimmed == '\t')) trimmed++;
        // Trim trailing newline
        size_t len = strlen(trimmed);
        if (len > 0 && trimmed[len-1] == '\n') trimmed[--len] = 0;

        if (len == 0) continue;

        if (trimmed[0] == '[' && trimmed[len-1] == ']') {
            if (current && current->frequency[0] != 0 && channel_count < MAX_CHANNELS) {
                // Previous channel complete
                // Actually the JS code pushes only if frequency is set
                // We are saving directly to array, just increment
                channel_count++;
            }
            if (channel_count >= MAX_CHANNELS) break;
            
            current = &channels[channel_count];
            memset(current, 0, sizeof(Channel));
            snprintf(current->name, sizeof(current->name), "%.*s", (int)len-2, trimmed+1);
        } else if (current && strchr(trimmed, '=')) {
            char *key = strtok(trimmed, "=");
            char *val = strtok(NULL, "=");
            if (key && val) {
                trim(key);
                trim(val);
                if (strcmp(key, "SERVICE_ID") == 0) strcpy(current->service_id, val);
                else if (strcmp(key, "FREQUENCY") == 0) strcpy(current->frequency, val);
                else if (strcmp(key, "VCHANNEL") == 0) strcpy(current->number, val);
            }
        }
    }
    
    // Push last channel
    if (current && current->frequency[0] != 0 && channel_count < MAX_CHANNELS) {
        channel_count++;
    }

    fclose(f);
    return channel_count;
}

Channel *find_channel_by_number(const char *number) {
    for(int i=0; i<channel_count; i++) {
        if (strcmp(channels[i].number, number) == 0) {
            return &channels[i];
        }
    }
    return NULL;
}
