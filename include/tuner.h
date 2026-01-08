#ifndef TUNER_H
#define TUNER_H

#include <sys/types.h>

typedef struct {
    int id;
    char path[64];
    int in_use;
    pid_t zap_pid;
    pid_t ffmpeg_pid;
    // Mutex could be added here for thread safety
} Tuner;

extern Tuner tuners[16];
extern int tuner_count;

void discover_tuners();
Tuner *acquire_tuner();
void release_tuner(Tuner *t);

#endif
