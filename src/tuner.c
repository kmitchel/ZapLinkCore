#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include "tuner.h"
#include "config.h"

Tuner tuners[MAX_TUNERS];
int tuner_count = 0;
int last_tuner_index = -1;

void discover_tuners() {
    DIR *d;
    struct dirent *dir;
    d = opendir("/dev/dvb");
    if (!d) {
        printf("Warning: /dev/dvb not found.\n");
        return;
    }

    while ((dir = readdir(d)) != NULL) {
        if (strncmp(dir->d_name, "adapter", 7) == 0) {
            int id = atoi(dir->d_name + 7);
            if (tuner_count < MAX_TUNERS) {
                tuners[tuner_count].id = id;
                snprintf(tuners[tuner_count].path, 64, "/dev/dvb/%s", dir->d_name);
                tuners[tuner_count].in_use = 0;
                tuners[tuner_count].zap_pid = 0;
                tuners[tuner_count].ffmpeg_pid = 0;
                tuner_count++;
            }
        }
    }
    closedir(d);
    
    // Sort logic could he added here
    printf("Discovered %d tuners.\n", tuner_count);
}

Tuner *acquire_tuner() {
    // Simple round robin
    if (tuner_count == 0) return NULL;

    for (int i = 0; i < tuner_count; i++) {
        int idx = (last_tuner_index + 1 + i) % tuner_count;
        if (!tuners[idx].in_use) {
            tuners[idx].in_use = 1;
            last_tuner_index = idx;
            return &tuners[idx];
        }
    }
    return NULL;
}

void release_tuner(Tuner *t) {
    if (t) {
        if (t->zap_pid > 0) {
            kill(t->zap_pid, SIGKILL);
            t->zap_pid = 0;
        }
        if (t->ffmpeg_pid > 0) {
             kill(t->ffmpeg_pid, SIGKILL);
             t->ffmpeg_pid = 0;
        }
        t->in_use = 0;
    }
}
