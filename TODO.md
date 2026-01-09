# ZapCore Project

**ZapCore** is a high-performance C-based backend application that serves a digital TV tuner (DVB) over HTTP, providing an M3U playlist, XMLTV guide, and streaming endpoints.

## Status: Production Ready 🚀
Core functionality, EPG parsing, concurrent scanning, preemption, and build systems are fully implemented and verified.

## TODO List

### Phase 1: Project Setup (Completed)
- [x] Create directory structure (`/opt/ZapCore`)
- [x] Initialize Git
- [x] Create Makefile
- [x] Setup Config/Logging modules

### Phase 2: Core Components (Completed)
- [x] Implement HTTP Server (Multi-threaded)
- [x] Implement Database Module (SQLite)
- [x] Implement Tuner Module (Discovery, Lifecycle)
- [x] Implement `channels.conf` parser
- [x] **Rename Project to ZapCore**

### Phase 3: Features (Completed)
- [x] `/xmltv.xml` and `/xmltv.json` endpoints
- [x] `/playlist.m3u` (Dynamic IP generation)
- [x] `/stream/:channelNum` (Preemption support)
- [x] Concurrent EPG Scanning (Parallel tuner usage)
- [x] Tuner Preemption (Stream priority)
- [x] CLI Arguments (`-p`, `-c`)
- [x] **mDNS Discovery** (Avahi integration)

### Phase 4: Reliability & Hardening (Completed)
- [x] Zombie process reaping (`waitpid`)
- [x] Graceful termination (`SIGTERM` -> `SIGKILL`)
- [x] Thread-safe tuner acquisition (Mutexes)
- [x] HTTP buffer safety and header parsing
- [x] EPG Data Sanity (Date validation, sanitization)
- [x] **Robust MSS Parsing** (ATSC string support)
- [x] **Huffman Decoding** (External `huffman.bin` module)

### Phase 5: Build & Deployment (Completed)
- [x] **Self-Contained Build** (`make setup`, `make local`)
- [x] **System Build** (`make`)
- [x] Helper Scripts (`setup_env.sh`)

### Future Work
- [ ] Add ffmpeg transcoding pipeline support (currently raw TS).
- [ ] Web UI for channel management (optional).
