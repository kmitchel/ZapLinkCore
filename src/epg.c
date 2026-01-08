#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <ctype.h> 
#include "epg.h"
#include "config.h"
#include "tuner.h"
#include "channels.h"
#include "db.h"

static pthread_t epg_tid;
static int epg_running = 0;

// Internal helpers
void scan_mux(Tuner *t, const char *frequency, const char *channel_name);
int parse_ts_chunk(const unsigned char *buf, size_t len, const char *freq);

// TS Packet size is 188
#define TS_PACKET_SIZE 188

// EPG Loop
void *epg_worker(void *arg) {
    (void)arg;
    
    // Initial delay to let server start
    sleep(5); 

    while (epg_running) {
        printf("[EPG] Starting scan cycle...\n");

        // 1. Identify unique frequencies (muxes)
        // Simple deduplication
        char scanned_freqs[MAX_CHANNELS][32];
        int scanned_count = 0;

        for (int i = 0; i < channel_count; i++) {
            if (!epg_running) break;

            Channel *c = &channels[i];
            
            // Check if already scanned
            int already = 0;
            for(int k=0; k<scanned_count; k++) {
                if (strcmp(scanned_freqs[k], c->frequency) == 0) {
                    already = 1;
                    break;
                }
            }
            if (already) continue;

            // Mark scanned
            strcpy(scanned_freqs[scanned_count++], c->frequency);

            // 2. Acquire Tuner
            Tuner *t = acquire_tuner();
            int retries = 5;
            while (!t && retries-- > 0) {
                if (!epg_running) break;
                sleep(1);
                t = acquire_tuner();
            }

            if (!t) {
                printf("[EPG] No tuners available. Skipping...\n");
                sleep(5);
                continue;
            }

            t->in_use = 1;
            // Scan Mux
            scan_mux(t, c->frequency, c->name);
            
            release_tuner(t);
            
            // Wait between muxes
            sleep(2);
        }

        printf("[EPG] Scan cycle complete. Sleeping 15 minutes...\n");
        // Sleep 15 mins (broken into chunks to allow exit)
        for(int k=0; k<15*60; k++) {
            if (!epg_running) break;
            sleep(1);
        }
    }
    return NULL;
}

void start_epg_thread() {
    if (epg_running) return;
    epg_running = 1;
    pthread_create(&epg_tid, NULL, epg_worker, NULL);
}

void stop_epg_thread() {
    epg_running = 0;
    pthread_join(epg_tid, NULL);
}

// -----------------------------------------------------------------------------
// TS / PSI Parser Implementation (Skeleton)
// -----------------------------------------------------------------------------

// Section Buffer for Reassembly
typedef struct {
    unsigned char buffer[4096];
    int len;
    int expected_len;
    int active;
} SectionBuffer;

// Map PIDs to Section Buffers (Simple Array for now, ATSC uses restricted PIDs mostly)
// PID 0x1FFB (8187) is the big one for ATSC.
SectionBuffer pid_buffers[8192];

// Helpers to parse tables
void parse_atsc_vct(unsigned char *section, int len, const char *freq);
void parse_atsc_eit(unsigned char *section, int len, const char *freq);
void parse_atsc_ett(unsigned char *section, int len, const char *freq);

void handle_section(int pid, unsigned char *section, int len, const char *freq) {
    if (len < 3) return;
    unsigned char table_id = section[0];
    
    if (pid == 0x1FFB) { // ATSC Base PID
        if (table_id == 0xC8 || table_id == 0xC9) {
            parse_atsc_vct(section, len, freq);
        } else if (table_id >= 0xCB && table_id <= 0xFB) {
             parse_atsc_eit(section, len, freq);
        }  else if (table_id >= 0xCC) {
             // ETT (Text) logic if table_id matches EIT + offset logic or explicit range
             // ATSC A/65: ETT 0xCC ... however strictly it's linked to EIT logic
             parse_atsc_ett(section, len, freq);
        }
    }
}

int parse_ts_chunk(const unsigned char *buf, size_t len, const char *freq) {
    int packet_count = 0;
    for (size_t i = 0; i < len - TS_PACKET_SIZE; i += TS_PACKET_SIZE) {
        if (buf[i] != 0x47) {
            // Re-sync?
            // For now assume aligned
            continue;
        }

        int tei = buf[i+1] & 0x80; // Transport Error Indicator
        if (tei) continue;

        int pusi = buf[i+1] & 0x40; // Payload Unit Start Indicator
        int pid = ((buf[i+1] & 0x1F) << 8) | buf[i+2];
        int adap = (buf[i+3] >> 4) & 0x3;
        int payload_offset = 4;

        if (adap == 0x2 || adap == 0x3) {
            int adap_len = buf[i+4];
            payload_offset += adap_len + 1;
        }

        if (payload_offset >= TS_PACKET_SIZE) continue;

        unsigned char *payload = (unsigned char*)buf + i + payload_offset;
        int payload_len = TS_PACKET_SIZE - payload_offset;

        // Skip non-interesting PIDs early
        if (pid != 0x1FFB) continue; 

        if (pusi) {
            if (payload_len < 1) continue;
            int pointer = payload[0];
            payload++; payload_len--;
            
            if (pointer < payload_len) {
                // If there was a previous section ending before pointer
                if (pid_buffers[pid].active) {
                    // Append remaining bytes
                    if (pid_buffers[pid].len + pointer < 4096) {
                        memcpy(pid_buffers[pid].buffer + pid_buffers[pid].len, payload, pointer);
                        handle_section(pid, pid_buffers[pid].buffer, pid_buffers[pid].len + pointer, freq);
                    }
                    pid_buffers[pid].active = 0;
                }

                // Start new section
                unsigned char *sec_start = payload + pointer;
                int sec_rem = payload_len - pointer;
                if (sec_rem >= 3) {
                    int section_len = ((sec_start[1] & 0x0F) << 8) | sec_start[2];
                    int total_len = section_len + 3;
                    
                    if (sec_rem >= total_len) {
                        // Full section in one packet
                        handle_section(pid, sec_start, total_len, freq);
                    } else {
                        // Buffer it
                        pid_buffers[pid].len = 0;
                        memcpy(pid_buffers[pid].buffer, sec_start, sec_rem);
                        pid_buffers[pid].len = sec_rem;
                        pid_buffers[pid].expected_len = total_len;
                        pid_buffers[pid].active = 1;
                    }
                }
            }
        } else {
             if (pid_buffers[pid].active) {
                 int needed = pid_buffers[pid].expected_len - pid_buffers[pid].len;
                 int to_copy = (payload_len < needed) ? payload_len : needed;
                 memcpy(pid_buffers[pid].buffer + pid_buffers[pid].len, payload, to_copy);
                 pid_buffers[pid].len += to_copy;

                 if (pid_buffers[pid].len >= pid_buffers[pid].expected_len) {
                     handle_section(pid, pid_buffers[pid].buffer, pid_buffers[pid].len, freq);
                     pid_buffers[pid].active = 0;
                 }
             }
        }
        packet_count++;
    }
    return packet_count;
}

// Map "Freq_SourceId" -> "Major.Minor"
// We need a simple hashmap or just a linear search array for this
// Since C is raw, we'll implement a simple list
typedef struct {
    char key[64];
    char val[16];
} SourceMap;

SourceMap source_map[256];
int source_map_count = 0;

void add_source_map(const char *freq, int source_id, const char *chan_num) {
    char key[64];
    snprintf(key, sizeof(key), "%s_%d", freq, source_id);
    
    // Update or Add
    for(int i=0; i<source_map_count; i++) {
        if (strcmp(source_map[i].key, key) == 0) return;
    }
    if (source_map_count < 256) {
        strcpy(source_map[source_map_count].key, key);
        strcpy(source_map[source_map_count].val, chan_num);
        source_map_count++;
    }
}

const char *get_source_map(const char *freq, int source_id) {
    char key[64];
    snprintf(key, sizeof(key), "%s_%d", freq, source_id);
    for(int i=0; i<source_map_count; i++) {
        if (strcmp(source_map[i].key, key) == 0) return source_map[i].val;
    }
    return NULL;
}

// -----------------------------------------------------------------------------
// ATSC Parsing Logic
// -----------------------------------------------------------------------------

void parse_atsc_vct(unsigned char *section, int len, const char *freq) {
    // VCT parsing logic
    // Skip header (table_id..protocol_version) -> offset 10
    int num_channels = section[9];
    int offset = 10;
    
    for (int i=0; i<num_channels; i++) {
        if (offset + 32 > len) break;
        // Major: byte 4 (hi 4) | byte 5 (hi 6 shifted) -> actually split differently
        // JS: major = ((section[offset + 4] & 0x0f) << 6) | (section[offset + 5] >> 2);
        // JS: minor = ((section[offset + 5] & 0x03) << 8) | section[offset + 6];
        int major = ((section[offset + 4] & 0x0f) << 6) | (section[offset + 5] >> 2);
        int minor = ((section[offset + 5] & 0x03) << 8) | section[offset + 6];
        int source_id = (section[offset + 22] << 8) | section[offset + 23];
        
        char chan_num[16];
        snprintf(chan_num, sizeof(chan_num), "%d.%d", major, minor);
        
        add_source_map(freq, source_id, chan_num);
        // printf("[EPG DEBUG] Mapped SourceID %d -> %s on %s\n", source_id, chan_num, freq);

        int desc_len = ((section[offset+30] & 0x03) << 8) | section[offset+31];
        offset += 32 + desc_len;
    }
}

void parse_atsc_eit(unsigned char *section, int len, const char *freq) {
    // EIT parsing
    // Header 10 bytes
    int source_id = (section[3] << 8) | section[4];
    int num_events = section[9];
    int offset = 10;

    const char *chan_num = get_source_map(freq, source_id);
    char fallback_num[32];
    if (!chan_num) {
         snprintf(fallback_num, sizeof(fallback_num), "%d", source_id);
         chan_num = fallback_num;
    }

    for (int i=0; i<num_events; i++) {
         if (offset + 10 > len) break;
         int event_id = ((section[offset] & 0x3F) << 8) | section[offset+1];
         unsigned int start_time = (section[offset+2] << 24) | (section[offset+3] << 16) | (section[offset+4] << 8) | section[offset+5];
         int duration = ((section[offset+6] & 0x0F) << 16) | (section[offset+7] << 8) | section[offset+8];
         int title_len = section[offset+9];
         
         // GPS base: 1980-01-06 00:00:00 UTC
         // JS calc: (startTimeGPS + 315964800 - 18) * 1000
         // 315964800 is diff between GPS epoch and UNIX epoch (1970) roughly? 
         // GPS epoch is Jan 6 1980. Unix is Jan 1 1970.
         // Diff is 10 years + leap days.
         // Let's trust JS math: unix_ts = gps + 315964800ULL
         // Subtract 18 leap seconds? TS usually includes leap seconds handling or not?
         // JS says - 18.
         
         long long start_ms = ((long long)start_time + 315964800ULL - 18) * 1000;
         long long end_ms = start_ms + (duration * 1000);

         char title[256] = {0};
         if (title_len > 0) {
             // Multiple String Structure
             // [num_strings][...strings]
             // Simply grabbing first string
             int str_offset = offset + 10;
             if (str_offset + title_len <= len) {
                 int num_strings = section[str_offset];
                 if (num_strings > 0) {
                     // 3 bytes lang code usually? NO, ATSC text structure is complex.
                     // JS: section[stringOffset + 6] is len.
                     // Header is usually [lang 3][segments 1][seg type 1][seg len 1]...
                     // Start simply.
                     // JS: stringOffset = 1. stringLen = buffer[stringOffset+6]?
                     // Wait, simple buffer dump often reveals string.
                     // Let's be safe.
                     // Logic: slice(str_offset + 1 + 6, ... len)
                     if (title_len > 7) {
                         int t_len = section[str_offset + 1 + 6];
                         int t_start = str_offset + 1 + 7;
                         if (t_len < sizeof(title) - 1 && t_start + t_len <= len) {
                             memcpy(title, section + t_start, t_len);
                             title[t_len] = 0;
                         }
                     }
                 }
             }
         }
         
         // Insert into DB
         if (strlen(title) > 0) {
             // We need an exposed DB insert function
             // "INSERT INTO programs (frequency, channel_service_id, start_time, end_time, title, event_id, source_id) ... "
             // We'll create a new DB function or execute raw SQL here if we expose db handle (not ideal)
             // Best to add `db_upsert_program` in db.c
             db_upsert_program(freq, chan_num, start_ms, end_ms, title, event_id, source_id);
             // printf("[EPG DEBUG] Found Event: %s (%s)\n", title, chan_num);
         }

         offset += 10 + title_len;
         // Clean descriptors?
         // In ATSC EIT, descriptors are NOT in the loop the same way?
         // Wait, "title_len" IS "length_in_bytes" of the title_text().
         // Then "descriptors_length" FOLLOWS title parsing? No.
         // Inspect ATSC Spec A/65.
         // event_loop_item: ... title_length, title_text(), descriptors_length, descriptors()
         // JS code: 
         // offset += 2 + descriptorsLength ??
         // Line 358: if (currentEventOffset + 2 <= section.length - 4) ... descriptorsLength = ...
         // My loop missed the descriptors length check.
         // Let's correct offset logic.
         
         // current_offset is now at end of title.
         // Next 12 bits are reserved + descriptors_length.
         int d_len_idx = offset; // offset is currently START of loop + 10 + title_len?
         // No. initialization `offset = 10`.
         // Inside loop: `offset` points to event_id.
         // `title_len` is at `offset + 9`.
         // `title_text` is `offset + 10` ... `offset + 10 + title_len`.
         // `descriptors_length` is at `offset + 10 + title_len` (12 bits).
         
         int after_title = offset + 10 + title_len;
         if (after_title + 2 <= len) {
             int desc_len = ((section[after_title] & 0x0F) << 8) | section[after_title+1];
             offset = after_title + 2 + desc_len;
         } else {
             break; 
         }
    }
}

void parse_atsc_ett(unsigned char *section, int len, const char *freq) {
    (void)section; (void)len; (void)freq;
    // TODO: Extended Text (Descriptions)
}

void scan_mux(Tuner *t, const char *frequency, const char *channel_name) {
    int pipefd[2];
    if (pipe(pipefd) == -1) return;

    printf("[EPG] Scanning Mux %s (%s) on Tuner %d\n", frequency, channel_name, t->id);

    pid_t pid = fork();
    if (pid == 0) {
        // Child
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        char adapter_id[8];
        snprintf(adapter_id, sizeof(adapter_id), "%d", t->id);
        
        // Scan for 15 seconds
        // dvbv5-zap -c ... -a ... -P -t 15 -o - channel_name
        execlp("dvbv5-zap", "dvbv5-zap", "-c", CHANNELS_CONF, "-a", adapter_id, "-P", "-t", "15", "-o", "-", channel_name, NULL);
        exit(1);
    } else if (pid > 0) {
        // Parent
        t->zap_pid = pid; // Track process to kill if needed
        close(pipefd[1]);

        unsigned char buf[4096 * 4];
        ssize_t n;
        while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
             parse_ts_chunk(buf, n, frequency);
        }

        close(pipefd[0]);
        waitpid(pid, NULL, 0);
        t->zap_pid = 0;
    }
}
