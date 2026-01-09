#ifndef EPG_H
#define EPG_H

extern int epg_skip_first;

void start_epg_thread();
void stop_epg_thread();
void wait_for_first_epg_scan();

#endif
