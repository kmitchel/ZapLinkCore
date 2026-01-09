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

int db_has_data() {
    if (!db) return 0;
    const char *sql = "SELECT COUNT(*) FROM programs;";
    sqlite3_stmt *stmt;
    int has_data = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            if (count > 0) has_data = 1;
        }
        sqlite3_finalize(stmt);
    }
    return has_data;
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

static void xml_escape_append(char **dest, size_t *size, size_t *cap, const char *src) {
    if (!src) return;
    for (const char *p = src; *p; p++) {
        switch (*p) {
            case '&':  append_str(dest, size, cap, "&amp;"); break;
            case '<':  append_str(dest, size, cap, "&lt;"); break;
            case '>':  append_str(dest, size, cap, "&gt;"); break;
            case '"':  append_str(dest, size, cap, "&quot;"); break;
            case '\'': append_str(dest, size, cap, "&apos;"); break;
            default:   append_str(dest, size, cap, (char[]){*p, 0}); break;
        }
    }
}

char *db_get_xmltv_programs() {
    if (!db) return NULL;

    sqlite3_stmt *stmt;
    // Order by Major.Minor numerical sort
    const char *sql = "SELECT title, description, start_time, end_time, channel_service_id FROM programs "
                      "ORDER BY CAST(SUBSTR(channel_service_id, 1, INSTR(channel_service_id, '.') - 1) AS INTEGER), "
                      "CAST(SUBSTR(channel_service_id, INSTR(channel_service_id, '.') + 1) AS INTEGER), start_time;";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return NULL;

    size_t cap = 512 * 1024; // 512KB for XMLTV
    size_t size = 0;
    char *xml = malloc(cap);
    if (!xml) return NULL;
    xml[0] = '\0';

    append_str(&xml, &size, &cap, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n");

    // Channel list
    for (int i = 0; i < channel_count; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "  <channel id=\"%s\">\n    <display-name>", channels[i].number);
        append_str(&xml, &size, &cap, buf);
        xml_escape_append(&xml, &size, &cap, channels[i].name);
        append_str(&xml, &size, &cap, "</display-name>\n  </channel>\n");
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *title = (const char *)sqlite3_column_text(stmt, 0);
        const char *desc = (const char *)sqlite3_column_text(stmt, 1);
        long long start = sqlite3_column_int64(stmt, 2);
        long long end = sqlite3_column_int64(stmt, 3);
        const char *svc_id = (const char *)sqlite3_column_text(stmt, 4); 

        // Format dates (YYYYMMDDHHMMSS +0000)
        time_t start_s = start / 1000;
        time_t end_s = end / 1000;
        struct tm *tm_s = gmtime(&start_s);
        char start_str[32];
        strftime(start_str, 32, "%Y%m%d%H%M%S +0000", tm_s);
        
        struct tm *tm_e = gmtime(&end_s);
        char end_str[32];
        strftime(end_str, 32, "%Y%m%d%H%M%S +0000", tm_e);

        char buf[512];
        snprintf(buf, sizeof(buf), "  <programme start=\"%s\" stop=\"%s\" channel=\"%s\">\n", 
                 start_str, end_str, svc_id ? svc_id : "");
        append_str(&xml, &size, &cap, buf);

        append_str(&xml, &size, &cap, "    <title>");
        xml_escape_append(&xml, &size, &cap, title);
        append_str(&xml, &size, &cap, "</title>\n");

        append_str(&xml, &size, &cap, "    <desc>");
        xml_escape_append(&xml, &size, &cap, desc);
        append_str(&xml, &size, &cap, "</desc>\n  </programme>\n");
    }

    append_str(&xml, &size, &cap, "</tv>");
    
    sqlite3_finalize(stmt);
    return xml;
}

// Helper to escape JSON strings
static void json_escape_append(char **dest, size_t *size, size_t *cap, const char *src) {
    if (!src) {
        append_str(dest, size, cap, "");
        return;
    }
    for (const char *p = src; *p; p++) {
        char buf[8];
        switch (*p) {
            case '"':  append_str(dest, size, cap, "\\\""); break;
            case '\\': append_str(dest, size, cap, "\\\\"); break;
            case '\n': append_str(dest, size, cap, "\\n"); break;
            case '\r': append_str(dest, size, cap, "\\r"); break;
            case '\t': append_str(dest, size, cap, "\\t"); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)*p);
                    append_str(dest, size, cap, buf);
                } else {
                    buf[0] = *p; buf[1] = '\0';
                    append_str(dest, size, cap, buf);
                }
                break;
        }
    }
}

char *db_get_json_programs() {
    if (!db) return NULL;

    char *sql = "SELECT title, description, start_time, end_time, channel_service_id FROM programs "
                "WHERE end_time > ? "
                "ORDER BY CAST(channel_service_id AS INTEGER), "
                "CASE WHEN INSTR(channel_service_id, '.') > 0 THEN CAST(SUBSTR(channel_service_id, INSTR(channel_service_id, '.') + 1) AS INTEGER) ELSE 0 END, "
                "start_time";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(db));
        return NULL;
    }

    // Bind current time
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    sqlite3_bind_int64(stmt, 1, now_ms);

    size_t cap = 1024 * 1024; // 1MB start
    size_t size = 0;
    char *json = malloc(cap);
    if (!json) return NULL;
    json[0] = '\0';

    append_str(&json, &size, &cap, "{\n  \"channels\": [\n");

    // Channels array
    for (int i = 0; i < channel_count; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "    {\"id\": \"%s\", \"name\": \"", channels[i].number);
        append_str(&json, &size, &cap, buf);
        json_escape_append(&json, &size, &cap, channels[i].name);
        append_str(&json, &size, &cap, "\"}");
        if (i < channel_count - 1) append_str(&json, &size, &cap, ",");
        append_str(&json, &size, &cap, "\n");
    }

    append_str(&json, &size, &cap, "  ],\n  \"programs\": [\n");

    // Programs array
    int first = 1;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        const char *title = (const char *)sqlite3_column_text(stmt, 0);
        const char *desc = (const char *)sqlite3_column_text(stmt, 1);
        long long start = sqlite3_column_int64(stmt, 2);
        long long end = sqlite3_column_int64(stmt, 3);
        const char *svc_id = (const char *)sqlite3_column_text(stmt, 4);

        if (!first) append_str(&json, &size, &cap, ",\n");
        first = 0;

        char buf[256];
        snprintf(buf, sizeof(buf), "    {\"channel\": \"%s\", \"start\": %lld, \"end\": %lld, \"title\": \"",
            svc_id ? svc_id : "", start, end);
        append_str(&json, &size, &cap, buf);
        json_escape_append(&json, &size, &cap, title);
        append_str(&json, &size, &cap, "\", \"description\": \"");
        json_escape_append(&json, &size, &cap, desc);
        append_str(&json, &size, &cap, "\"}");
    }

    append_str(&json, &size, &cap, "\n  ]\n}");
    
    sqlite3_finalize(stmt);
    return json;
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

void db_update_program_description(const char *frequency, const char *channel_service_id, int event_id, const char *description) {
    if (!db || !description || description[0] == '\0') return;

    char *sql = "UPDATE programs SET description = ? WHERE frequency = ? AND channel_service_id = ? AND event_id = ?";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    sqlite3_bind_text(stmt, 1, description, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, frequency, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, channel_service_id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, event_id);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// Delete program entries that ended more than 24 hours ago
int db_cleanup_expired() {
    if (!db) return 0;

    // Calculate cutoff time: 24 hours ago in milliseconds
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    long long cutoff_ms = now_ms - (24LL * 60 * 60 * 1000); // 24 hours ago

    char *sql = "DELETE FROM programs WHERE end_time < ?";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cleanup prepare error: %s\n", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_int64(stmt, 1, cutoff_ms);
    rc = sqlite3_step(stmt);
    int deleted = sqlite3_changes(db);
    sqlite3_finalize(stmt);

    if (deleted > 0) {
        printf("[DB] Cleaned up %d expired program entries\n", deleted);
    }
    return deleted;
}
