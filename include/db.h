/**
 * @file db.h
 * @brief SQLite database interface for EPG storage
 * 
 * Provides persistent storage for Electronic Program Guide data.
 * Programs are stored with frequency + channel + start_time as the
 * composite key, enabling accurate per-mux program tracking.
 */

#ifndef DB_H
#define DB_H

/**
 * Initialize the database connection and create tables if needed
 * @return 1 on success, 0 on failure
 */
int db_init();

/**
 * Close the database connection
 */
void db_close();

/**
 * Check if the database has any program data
 * Used to determine if first EPG scan can be skipped
 * @return 1 if data exists, 0 if empty
 */
int db_has_data();

/**
 * Generate XMLTV-formatted program guide
 * Includes channel list and all programs ordered by channel/time
 * @return Allocated XML string (caller must free), or NULL on error
 */
char *db_get_xmltv_programs();

/**
 * Generate JSON-formatted program guide
 * Returns future programs only (end_time > now)
 * @return Allocated JSON string (caller must free), or NULL on error
 */
char *db_get_json_programs();

/**
 * Insert or update a program entry
 * Uses UPSERT semantics: updates if key exists, inserts otherwise
 * 
 * @param frequency         RF frequency (key component)
 * @param channel_service_id Virtual channel number "X.Y"
 * @param start_time        Start time in ms since epoch (key component)
 * @param end_time          End time in ms since epoch
 * @param title             Program title
 * @param event_id          ATSC event ID (for ETT matching)
 * @param source_id         ATSC source ID
 */
void db_upsert_program(const char *frequency, const char *channel_service_id, 
                       long long start_time, long long end_time, 
                       const char *title, int event_id, int source_id);

/**
 * Update program description from ETT (Extended Text Table)
 * Matches by frequency, channel, and event_id
 */
void db_update_program_description(const char *frequency, 
                                   const char *channel_service_id, 
                                   int event_id, const char *description);

/**
 * Delete program entries that ended more than 24 hours ago
 * Called periodically to prevent database bloat
 * @return Number of entries deleted
 */
int db_cleanup_expired();

#endif
