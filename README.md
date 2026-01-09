# ZapLinkCore

**ZapLinkCore** is a high-performance C-based backend engine for the **ZapLink** ecosystem. 
It serves a digital TV tuner (DVB/ATSC) over HTTP, providing rock-solid streaming, real-time transcoding, a standards-compliant XMLTV guide, and seamless network discovery.

## 🌟 The ZapLink Ecosystem
ZapLinkCore is the backbone of a multi-platform TV experience:
- **ZapLinkCore**: The high-performance C backend (This project).
- **ZapLinkWeb**: A modern web-based client for browser viewing.
- **ZapLinkTV**: A native AndroidTV client for the big screen.

## 🚀 Key Features

### **Streaming & Transcoding**
- **Raw MPEG-TS**: `/stream/{channel}` – Direct passthrough from tuner.
- **Real-time Transcoding**: `/transcode/{backend}/{codec}/{channel}` – On-the-fly encoding.
  - **Backends**: `software`, `qsv` (Intel), `nvenc` (NVIDIA), `vaapi`
  - **Codecs**: `h264`, `hevc`, `av1`
  - **5.1 Audio**: Append `/6` for surround sound (e.g., `/transcode/qsv/h264/55.1/6`)
- **Intelligent Preemption**: Streams automatically pause background EPG scans.

### **Advanced EPG Engine**
- **Robust MSS Parsing**: Correctly handles ATSC **Multiple String Structures**.
- **Huffman Decompression**: Native support for **ATSC A/65 Huffman coding**.
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

# Copy your channel config
sudo cp channels.conf /etc/zaplink/

# Start the service
sudo systemctl enable --now zaplinkcore
```

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
| `/transcode/{backend}/{codec}/{channel}` | Transcoded stream (stereo) |
| `/transcode/{backend}/{codec}/{channel}/6` | Transcoded stream (5.1 surround) |
| `/playlist.m3u` | M3U playlist (raw streams) |
| `/playlist/{backend}/{codec}.m3u` | M3U playlist (transcoded) |
| `/xmltv.xml` | XMLTV EPG guide |
| `/xmltv.json` | JSON EPG guide |

### Examples
```bash
# Raw stream
curl http://localhost:18392/stream/55.1 | vlc -

# Transcoded H.264 (software, stereo)
curl http://localhost:18392/transcode/software/h264/55.1 | vlc -

# Intel QSV HEVC with 5.1 audio
curl http://localhost:18392/transcode/qsv/hevc/55.1/6 | vlc -

# AV1 (WebM container)
curl http://localhost:18392/transcode/software/av1/55.1 | vlc -
```

---

## 🛠️ Configuration

| File | Location | Description |
|------|----------|-------------|
| `channels.conf` | `/etc/zaplink/` | DVB channel list |
| `huffman.bin` | `/etc/zaplink/` | Huffman decode tables |
| `epg.db` | `/etc/zaplink/` | SQLite EPG database (auto-created) |

### Command Line Options
```
./zaplinkcore [options]
  -p <port>   Port to listen on (default: 18392)
  -h          Show usage
```

---

## 📂 Project Structure
- `src/` – Source code
  - `main.c` – Entry point & lifecycle
  - `transcode.c` – FFmpeg transcoding pipeline
  - `epg.c` – ATSC/DVB parser
  - `http_server.c` – Multi-threaded HTTP engine
  - `tuner.c` – Hardware resource management
  - `mdns.c` – Avahi/mDNS integration
- `include/` – Header files
- `support/` – Helper scripts
- `zaplinkcore.service` – Systemd unit file
