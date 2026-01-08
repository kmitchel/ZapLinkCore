#ifndef DB_H
#define DB_H

int db_init();
void db_close();
// Caller must free the returned string
char *db_get_xmltv_programs();

void db_upsert_program(const char *frequency, const char *channel_service_id, long long start_time, long long end_time, const char *title, int event_id, int source_id);

#endif
