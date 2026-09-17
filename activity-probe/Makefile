PS5_PAYLOAD_SDK ?= /tmp/ps5-payload-sdk-install
LLVM_CONFIG ?= /opt/homebrew/opt/llvm@18/bin/llvm-config
export LLVM_CONFIG
.DEFAULT_GOAL := all

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include plugin.mk

ELF := activity-probe.elf
SQLITE_PREFIX ?= $(PS5_SYSROOT)$(PS5_HBROOT)
SQLITE_CFLAGS ?= -I$(SQLITE_PREFIX)/include
SQLITE_LDFLAGS ?= -L$(SQLITE_PREFIX)/lib
SQLITE_LIBS ?= -lsqlite3
CFLAGS := -Wall -Wextra -Werror -O2 $(SQLITE_CFLAGS) \
	-DPROBE_VERSION=\"0.7.0\" \
	-DTRACKER_VERSION=\"1.64\" -DDASHBOARD_VERSION=\"1.49.0\"

.PHONY: all clean plugin check-sqlite

all: $(ELF)

check-sqlite:
	@test -f "$(SQLITE_PREFIX)/include/sqlite3.h" || \
		(echo "Missing sqlite3.h; install ps5-payload-sqlite or set SQLITE_PREFIX" >&2; exit 1)
	@test -f "$(SQLITE_PREFIX)/lib/libsqlite3.a" || \
		(echo "Missing libsqlite3.a; install ps5-payload-sqlite or set SQLITE_PREFIX" >&2; exit 1)


$(ELF): check-sqlite main.c app_focus_monitor.c app_focus_monitor.h shell_ui_monitor.c shell_ui_monitor.h game_metadata.c game_metadata.h game_library.c game_library.h tracker.c tracker.h http_server.c http_server.h \
		offline_update.c offline_update.h
	$(CC) $(CFLAGS) $(SQLITE_LDFLAGS) -o $@ main.c app_focus_monitor.c shell_ui_monitor.c game_metadata.c game_library.c tracker.c http_server.c offline_update.c $(SQLITE_LIBS)
	$(STRIP) --strip-all $@

clean:
	rm -f $(ELF) $(PLUGIN)
