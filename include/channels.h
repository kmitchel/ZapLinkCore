#ifndef CHANNELS_H
#define CHANNELS_H

typedef struct {
    char name[64];
    char service_id[32];
    char frequency[32];
    char number[32];
} Channel;

extern Channel channels[200];
extern int channel_count;

int load_channels(const char *filename);
Channel *find_channel_by_number(const char *number);

#endif
