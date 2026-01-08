# C Tuner Project

This project duplicates the functionality of the Node.js jellyfin-tuner application in C.

## Status: Alpha
Core functionality is implemented but requires rigorous testing on actual hardware.

## TODO List

### Phase 1: Project Setup (Completed)
- [x] Create directory structure (`/opt/c_tuner`)
- [x] Initialize Git
- [x] Create Makefile
- [x] Setup Config/Logging modules

### Phase 2: Core Components (Completed)
- [x] Implement HTTP Server (Multi-threaded)
    - [x] Request Parsing
    - [x] Route Dispatching
- [x] Implement Database Module
    - [x] Connect to `epg.db`
    - [x] `db_get_xmltv_programs`
- [x] Implement Tuner Module
    - [x] Discover adapters
    - [x] Manage tuner state
    - [x] Implement `channels.conf` parser

### Phase 3: Features (Drafted)
- [x] `/xmltv.xml` endpoint
    - [x] Functional generation from DB
- [x] `/playlist.m3u` endpoint
    - [x] Functional generation from channels list
- [x] `/stream/:channelNum` endpoint
    - [x] Maps channel number -> Channel Name
    - [x] Spawns `dvbv5-zap`
    - [x] Pipes output to socket
    - [x] Handles process cleanup on disconnect

### Future Work
- [ ] **Robustness**:
    - [ ] Add signal handling for child processes (waitpid to avoid zombies).
    - [ ] Improve HTTP parsing (handle partial headers, large headers).
    - [ ] Add ffmpeg transcoding pipeline support (currently just raw stream).
- [ ] **EPG Updates**:
    - [ ] Implement EPG grabbing loop (currently just reads existing DB).
    - [ ] Port `scan_helper.js` logic if needed.
- [ ] **Configuration**:
    - [ ] Load config from `env.sh` or `.env` files.
    - [ ] Make port configurable via command line args.
- [ ] **Testing**:
    - [ ] Verify `dvbv5-zap` arguments on real hardware.
    - [ ] Test concurrent stream limits.

### Reference
- Source: `/opt/jellyfin-tuner`
