#ifndef CHANNELS_H
#define CHANNELS_H

#include <sys/types.h>
#include "config.h"

typedef struct {
    char name[64];
    char service_id[32];
    char frequency[32];
    char number[32];
} Channel;

extern Channel channels[MAX_CHANNELS];
extern int channel_count;

extern char channels_conf_path[512];
int load_channels(const char *filename);
Channel *find_channel_by_number(const char *number);

#endif
