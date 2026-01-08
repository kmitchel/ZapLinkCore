# C Tuner

A C implementation of the jellyfin-tuner application.
This application serves a digital TV tuner (DVB) over HTTP, providing an M3U playlist, XMLTV guide, and streaming endpoints.

## Features
- **HTTP Server**: Hand-rolled threaded server (No external web framework deps).
- **M3U Playlist**: `/playlist.m3u` generated from `channels.conf`.
- **XMLTV Guide**: `/xmltv.xml` generated from `epg.db` (SQLite).
- **Streaming**: `/stream/<channel_number>` pipes `dvbv5-zap` output to the client.

## Build Requirements
- GCC
- Make
- sqlite3 (`libsqlite3-dev`)
- pthread (Standard with glibc)
- dvbv5-zap (from `v4l-utils` or `dvb-apps`)

## Usage

1. **Build**:
   ```bash
   make
   ```

2. **Configuration**:
   - Ensure `channels.conf` is present in the working directory.
   - Ensure `epg.db` is present (populated by the existing Node.js app's EPG scraper or similar).

3. **Run**:
   ```bash
   ./build/c_tuner
   ```
   Server listens on Port 5000 (Default).

4. **Install as Service**:
   Create a systemd unit similar to `jellyfin-tuner.service`, pointing to the new binary.

## Structure
- `src/`: Source code
  - `main.c`: Entry point and initialization.
  - `http_server.c`: Socket server and request handling.
  - `tuner.c`: Tuner discovery and management.
  - `channels.c`: Configuration parsing.
  - `db.c`: Database access.
- `include/`: Header files.
- `build/`: Output artifacts.
