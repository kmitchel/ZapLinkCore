#!/bin/bash
set -e

# Configuration
DEPS_DIR="$(pwd)/deps"
SQLITE_YEAR="2023"
SQLITE_VERSION="3440000" # 3.44.0
AVAHI_VERSION="0.8"

# URLs
SQLITE_URL="https://www.sqlite.org/${SQLITE_YEAR}/sqlite-amalgamation-${SQLITE_VERSION}.zip"
AVAHI_URL="https://github.com/lathiat/avahi/releases/download/v${AVAHI_VERSION}/avahi-${AVAHI_VERSION}.tar.gz"

mkdir -p "$DEPS_DIR/src"
mkdir -p "$DEPS_DIR/include"
mkdir -p "$DEPS_DIR/lib"

function download_libs() {
    echo "[SETUP] Downloading dependencies..."
    
    # SQLite
    if [ ! -f "$DEPS_DIR/src/sqlite.zip" ]; then
        echo "  - Fetching SQLite..."
        wget -q -O "$DEPS_DIR/src/sqlite.zip" "$SQLITE_URL"
    fi

    # Avahi
    if [ ! -f "$DEPS_DIR/src/avahi.tar.gz" ]; then
        echo "  - Fetching Avahi..."
        wget -q -O "$DEPS_DIR/src/avahi.tar.gz" "$AVAHI_URL" || echo "Warning: Failed to download Avahi. System libs might be needed."
    fi
}

function build_sqlite() {
    echo "[SETUP] Building SQLite..."
    cd "$DEPS_DIR/src"
    unzip -q -o sqlite.zip
    cd "sqlite-amalgamation-${SQLITE_VERSION}"
    
    # Compile static lib
    gcc -O2 -c sqlite3.c
    ar rcs "$DEPS_DIR/lib/libsqlite3.a" sqlite3.o
    
    # Copy headers
    cp sqlite3.h sqlite3ext.h "$DEPS_DIR/include/"
    echo "[SETUP] SQLite built successfully."
}

function build_avahi() {
    echo "[SETUP] Building Avahi (Client & Common)..."
    if [ ! -f "$DEPS_DIR/src/avahi.tar.gz" ]; then
        echo "[SETUP] Avahi source not found, skipping local build."
        return
    fi
    
    cd "$DEPS_DIR/src"
    tar -xf avahi.tar.gz
    cd "avahi-${AVAHI_VERSION}"
    
    # Configure mostly disabled, just want client libs
    # Note: This requires standard build tools and likely libdbus-1-dev / expat / libdaemon on host
    ./configure --prefix="$DEPS_DIR" \
        --disable-glib \
        --disable-gobject \
        --disable-qt3 \
        --disable-qt4 \
        --disable-gtk \
        --disable-gtk3 \
        --disable-python \
        --disable-mono \
        --disable-monodoc \
        --disable-doxygen-doc \
        --disable-xmltoman \
        --with-distro=none \
        --disable-manpages
        
    make -j$(nproc)
    make install
    echo "[SETUP] Avahi built successfully."
}

function main() {
    download_libs
    build_sqlite
    # build_avahi # Commented out by default as it's often too complex for simple setups without system deps
    # If the user really wants it, they can uncomment. Currently primarily providing SQLite.
    
    echo "--------------------------------------------------------"
    echo "Setup complete."
    echo "Libraries installed in: $DEPS_DIR"
    echo "Use 'make local' to build against these libraries."
    echo "--------------------------------------------------------"
}

main
