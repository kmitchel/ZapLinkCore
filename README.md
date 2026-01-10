# ZapLinkCore

**ZapLinkCore** is a high-performance C-based backend engine for the **ZapLink** ecosystem. 
It serves a digital TV tuner (DVB/ATSC) over HTTP, providing rock-solid MPEG-TS streaming, a standards-compliant XMLTV guide, and seamless network discovery.

## 🌟 The ZapLink Ecosystem
ZapLinkCore is the backbone of a multi-platform TV experience:
- **ZapLinkCore**: The high-performance C backend (This project).
- **ZapLinkWeb**: A modern web-based client for browser viewing (handles transcoding).
- **ZapLinkTV**: A native AndroidTV client for the big screen.

## 🚀 Key Features

### **Raw MPEG-TS Streaming**
- **Raw MPEG-TS**: `/stream/{channel}` – Direct passthrough from tuner.
- **Intelligent Preemption**: Streams automatically pause background EPG scans.
- **M3U Playlist**: `/playlist.m3u` – Compatible with VLC, Jellyfin, etc.

### **Advanced EPG Engine**
- **Robust MSS Parsing**: Correctly handles ATSC **Multiple String Structures**.
- **Lazy Huffman Loading**: Tables loaded only when Huffman-coded content is detected.
- **Concurrent Scanning**: Utilizes all available tuners in parallel.

### **Zero-Conf Networking**
- **mDNS Discovery**: Advertises as **"ZapLinkCore"** (`_http._tcp`).

---

## 📦 Installation

### Quick Install (Recommended)
```bash
# Build
make

# Install (creates user, directories, service)
sudo make install

# Start the service
sudo systemctl enable --now zaplinkcore
```

On first run, ZapLinkCore will launch an **interactive channel scanner** that:
1. Detects available tuners
2. Queries your ZIP code for nearby stations
3. Scans frequencies in parallel across all tuners
4. Generates `channels.conf` automatically

> **Already have a channels.conf?** Copy it to `/opt/zaplink/` before starting the service.

### Self-Contained Build
Downloads and compiles dependencies locally:
```bash
make setup
make local
```

### Uninstall
```bash
sudo make uninstall
```

---

## 🔌 API Endpoints

| Endpoint | Description |
|----------|-------------|
| `/stream/{channel}` | Raw MPEG-TS passthrough |
| `/playlist.m3u` | M3U playlist (raw streams) |
| `/xmltv.xml` | XMLTV EPG guide |
| `/xmltv.json` | JSON EPG guide |

### Examples
```bash
# Raw stream
curl http://localhost:18392/stream/55.1 | vlc -

# Get M3U playlist
curl http://localhost:18392/playlist.m3u > channels.m3u

# Get EPG data
curl http://localhost:18392/xmltv.xml
curl http://localhost:18392/xmltv.json
```

---

## 🛠️ Configuration

| File | Location | Description |
|------|----------|-------------|
| `channels.conf` | `/opt/zaplink/` | DVB channel list |
| `huffman.bin` | `/opt/zaplink/` | Huffman decode tables |
| `epg.db` | `/opt/zaplink/` | SQLite EPG database (auto-created) |

### Command Line Options
```
./zaplinkcore [options]
  -p <port>   Port to listen on (default: 18392)
  -v          Enable verbose/debug logging
  -h          Show usage
```

---

## � Jellyfin Integration

ZapLinkCore is designed to work seamlessly as a **tuner backend for Jellyfin**, allowing Jellyfin to handle transcoding with its mature, well-tested pipeline.

### Setup

1. In Jellyfin, go to **Dashboard → Live TV → Tuner Devices**
2. Click **Add** and select **M3U Tuner**
3. Enter the playlist URL:
   ```
   http://<zaplinkcore-ip>:18392/playlist.m3u
   ```
4. Go to **TV Guide Data Providers** and add an **XMLTV** source:
   ```
   http://<zaplinkcore-ip>:18392/xmltv.xml
   ```
5. Refresh the guide data and you're ready to watch!

### Why This Architecture?

ZapLink follows a **modular philosophy** that separates concerns:

| Component | Responsibility |
|-----------|----------------|
| **ZapLinkCore** | Tuner access, raw streaming, EPG collection |
| **Jellyfin/Emby** | DVR, transcoding, client delivery |
| **ZapLinkWeb** | Streaming, flexible transcoding, EPG viewer, DVR |
| **ZapLinkTV** | AndroidTV native playback |

This design gives you **flexibility in where transcoding happens**:

- **Let Jellyfin transcode**: Use ZapLinkCore's raw MPEG-TS streams. Jellyfin's Hardware Acceleration (QSV, NVENC, VAAPI) handles conversion to your client's preferred format.
- **Use ZapLinkWeb**: For direct browser access without Jellyfin, ZapLinkWeb can handle transcoding independently.
- **Direct playback**: VLC and other capable players can consume the raw streams directly—no transcoding needed.

By keeping ZapLinkCore focused on **reliable tuner access and EPG**, you avoid duplicating transcoding logic and can leverage Jellyfin's extensive client compatibility and hardware acceleration support.

### Troubleshooting: EPG Not Showing

If Jellyfin shows channels but no program guide:

1. **Delete and re-add the XMLTV provider** (editing the URL alone may not work)
   - Dashboard → Live TV → TV Guide Data Providers
   - Delete the existing XMLTV source
   - Add a new XMLTV source with the URL
   - Click Refresh Guide Data

2. **Verify channel mapping**
   - Dashboard → Live TV → Channels
   - Ensure each channel has an EPG source mapped

3. **Check the XMLTV output directly**
   ```bash
   curl http://localhost:18392/xmltv.xml | head -20
   ```

---

## �📂 Project Structure
- `src/` – Source code
  - `main.c` – Entry point & lifecycle
  - `epg.c` – ATSC/DVB parser
  - `http_server.c` – Multi-threaded HTTP engine
  - `tuner.c` – Hardware resource management
  - `mdns.c` – Avahi/mDNS integration
  - `log.h` – Pretty console logging system
- `include/` – Header files
- `support/` – Helper scripts
- `docs/` – Documentation & archived transcoding source
- `zaplinkcore.service` – Systemd unit file

---

## 📝 Archived Transcoding

Transcoding source code is preserved in `docs/` for potential use in ZapLinkWeb:
- [`docs/TRANSCODING.md`](docs/TRANSCODING.md) – Complete technical reference
- `docs/transcode.c`, `docs/hls.c` – Archived implementations

