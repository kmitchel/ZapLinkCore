#ifndef TUNER_H
#define TUNER_H

#include <sys/types.h>

typedef enum {
    USER_NONE = 0,
    USER_STREAM,
    USER_EPG
} TunerUser;

typedef struct {
    int id;
    char path[512];
    int in_use;
    pid_t zap_pid;
    pid_t ffmpeg_pid;
    TunerUser user_type;
} Tuner;

extern Tuner tuners[16];
extern int tuner_count;

void discover_tuners();
Tuner *acquire_tuner(TunerUser purpose);
void release_tuner(Tuner *t);

#endif
