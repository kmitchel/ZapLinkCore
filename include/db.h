#ifndef DB_H
#define DB_H

int db_init();
void db_close();
int db_has_data();
// Caller must free the returned string
char *db_get_xmltv_programs();
// Caller must free the returned string (JSON format)
char *db_get_json_programs();

void db_upsert_program(const char *frequency, const char *channel_service_id, long long start_time, long long end_time, const char *title, int event_id, int source_id);
void db_update_program_description(const char *frequency, const char *channel_service_id, int event_id, const char *description);
// Delete entries older than 24 hours, returns number deleted
int db_cleanup_expired();

#endif
