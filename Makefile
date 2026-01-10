CC = gcc
CFLAGS = -Wall -Wextra -I./include -g $(shell pkg-config --cflags avahi-client)
LDFLAGS = -lsqlite3 -lpthread $(shell pkg-config --libs avahi-client)

SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

TARGET = $(BUILD_DIR)/zaplinkcore

all: $(TARGET)

# Build Modes
# run 'make local' to build against deps/ (created by 'make setup')
# run 'make' to build against system libs

ifeq ($(MAKECMDGOALS),local)
    CFLAGS += -I./deps/include
    LDFLAGS = -L./deps/lib -lsqlite3 -lpthread $(shell pkg-config --libs avahi-client)
    # Note: If local avahi build is enabled in setup_env.sh, we'd add -L here too.
    # Currently assuming system avahi for 'local' unless modified.
else
    CFLAGS += $(shell pkg-config --cflags avahi-client)
    LDFLAGS = -lsqlite3 -lpthread $(shell pkg-config --libs avahi-client)
endif

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

setup:
	@./support/setup_env.sh

cleanup:
	rm -rf deps
	rm -rf $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

local: $(TARGET)

# Installation paths - all files live under /opt/zaplink
INSTALL_DIR = /opt/zaplink
BINDIR = $(INSTALL_DIR)
CONFDIR = $(INSTALL_DIR)
SERVICEFILE = zaplinkcore.service

install: $(TARGET)
	@echo "Creating install directory..."
	@mkdir -p $(INSTALL_DIR)
	@echo "Creating zaplink user..."
	@id -u zaplink &>/dev/null || useradd -r -s /usr/sbin/nologin -d $(INSTALL_DIR) zaplink
	@echo "Installing binary..."
	@install -m 755 -o zaplink -g zaplink $(TARGET) $(BINDIR)/zaplinkcore
	@echo "Installing support files..."
	@test -f huffman.bin && install -m 644 -o zaplink -g zaplink huffman.bin $(CONFDIR)/ || true
	@echo "Setting directory ownership..."
	@chown zaplink:zaplink $(INSTALL_DIR)
	@echo "Installing systemd service..."
	@install -m 644 $(SERVICEFILE) /etc/systemd/system/
	@systemctl daemon-reload
	@echo ""
	@echo "Installation complete!"
	@echo "  Install directory: $(INSTALL_DIR)"
	@echo "  Binary: $(BINDIR)/zaplinkcore"
	@echo ""
	@echo "Next steps:"
	@echo "  1. Run: sudo systemctl enable --now zaplinkcore"
	@echo "  2. On first run, the built-in channel scanner will guide you"
	@echo "     through tuner selection and frequency scanning."
	@echo ""
	@echo "Or, manually copy an existing channels.conf to $(CONFDIR)/"

uninstall:
	@echo "Stopping service..."
	-@systemctl stop zaplinkcore 2>/dev/null || true
	-@systemctl disable zaplinkcore 2>/dev/null || true
	@echo "Removing files..."
	@rm -f /etc/systemd/system/$(SERVICEFILE)
	@rm -f $(BINDIR)/zaplinkcore
	@systemctl daemon-reload
	@echo "Uninstall complete. Config directory $(CONFDIR) preserved."

.PHONY: all clean setup cleanup local install uninstall
