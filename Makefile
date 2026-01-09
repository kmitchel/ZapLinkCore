CC = gcc
CFLAGS = -Wall -Wextra -I./include -g $(shell pkg-config --cflags avahi-client)
LDFLAGS = -lsqlite3 -lpthread $(shell pkg-config --libs avahi-client)

SRC_DIR = src
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

TARGET = $(BUILD_DIR)/zapcore

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

.PHONY: all clean setup cleanup local
