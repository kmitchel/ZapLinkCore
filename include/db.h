#ifndef DB_H
#define DB_H

int db_init();
void db_close();
// Caller must free the returned string
char *db_get_xmltv_programs();

#endif
