#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include "db.h"
#include "config.h"
#include "channels.h"

sqlite3 *db = NULL;

int db_init() {
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    
    // Create Table if not exists
    char *sql = "CREATE TABLE IF NOT EXISTS programs ("
                "frequency TEXT, "
                "channel_service_id TEXT, "
                "start_time INTEGER, "
                "end_time INTEGER, "
                "title TEXT, "
                "description TEXT, "
                "event_id INTEGER, "
                "source_id INTEGER, "
                "PRIMARY KEY (frequency, channel_service_id, start_time));";
    
    char *err_msg = 0;
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return 0;
    }
    
    return 1;
}

void db_close() {
    if (db) sqlite3_close(db);
}

// Helper to append to dynamic string
void append_str(char **dest, size_t *size, size_t *cap, const char *src) {
    size_t len = strlen(src);
    if (*size + len + 1 > *cap) {
        *cap = (*size + len + 1) * 2;
        *dest = realloc(*dest, *cap);
    }
    strcpy(*dest + *size, src);
    *size += len;
}

char *db_get_xmltv_programs() {
    if (!db) return NULL;

    char *sql = "SELECT title, description, start_time, end_time, channel_service_id FROM programs WHERE end_time > ? ORDER BY frequency, channel_service_id, start_time";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    // Bind current time (in ms, as JS uses Date.now())
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    sqlite3_bind_int64(stmt, 1, now_ms);

    size_t cap = 1024 * 1024; // 1MB start
    size_t size = 0;
    char *xml = malloc(cap);
    if (!xml) return NULL;
    xml[0] = '\0';

    append_str(&xml, &size, &cap, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n");

    // Channels
    for (int i=0; i<channel_count; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "  <channel id=\"%s\">\n    <display-name>%s</display-name>\n  </channel>\n", 
            channels[i].number, channels[i].name);
        append_str(&xml, &size, &cap, buf);
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *title = (const char *)sqlite3_column_text(stmt, 0);
        const char *desc = (const char *)sqlite3_column_text(stmt, 1);
        long long start = sqlite3_column_int64(stmt, 2);
        long long end = sqlite3_column_int64(stmt, 3);
        const char *svc_id = (const char *)sqlite3_column_text(stmt, 4); 
        
        // Find channel Number. In DB we stored 'channel_service_id' which in JS logic could be 'Major.Minor' OR 'ServiceId'.
        // Wait, in my parse_atsc_EIT, I passed 'chan_num' (Major.Minor) as 'channel_service_id'.
        // So I can use it directly as channel ID if it matches `channels.conf` VCHANNEL.
        // JS logic stores `channel_service_id` via `this.sourceMap.get(...)` which maps to channelNum.
        // So svc_id IS likely "55.1".

        // Format dates (YYYYMMDDHHMMSS +0000)
        time_t start_s = start / 1000;
        time_t end_s = end / 1000;
        struct tm *tm_s = gmtime(&start_s);
        char start_str[20];
        strftime(start_str, 20, "%Y%m%d%H%M%S +0000", tm_s);
        
        struct tm *tm_e = gmtime(&end_s);
        char end_str[20];
        strftime(end_str, 20, "%Y%m%d%H%M%S +0000", tm_e);

        char buf[1024];
        // TODO: XML Escape
        snprintf(buf, sizeof(buf), "  <programme start=\"%s\" stop=\"%s\" channel=\"%s\">\n    <title>%s</title>\n    <desc>%s</desc>\n  </programme>\n",
            start_str, end_str, svc_id, title ? title : "", desc ? desc : "");
            
        append_str(&xml, &size, &cap, buf);
    }

    append_str(&xml, &size, &cap, "</tv>");
    
    sqlite3_finalize(stmt);
    return xml;
}

void db_upsert_program(const char *frequency, const char *channel_service_id, long long start_time, long long end_time, const char *title, int event_id, int source_id) {
    if (!db) return;

    char *sql = "INSERT INTO programs (frequency, channel_service_id, start_time, end_time, title, description, event_id, source_id) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
                "ON CONFLICT(frequency, channel_service_id, start_time) "
                "DO UPDATE SET title=excluded.title, end_time=excluded.end_time, event_id=excluded.event_id, source_id=excluded.source_id";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, frequency, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, channel_service_id, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, start_time);
    sqlite3_bind_int64(stmt, 4, end_time);
    sqlite3_bind_text(stmt, 5, title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, "", -1, SQLITE_STATIC); // Description empty for now
    sqlite3_bind_int(stmt, 7, event_id);
    sqlite3_bind_int(stmt, 8, source_id);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
