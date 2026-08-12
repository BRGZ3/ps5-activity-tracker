PS5_PAYLOAD_SDK ?= /tmp/ps5-payload-sdk-install
LLVM_CONFIG ?= /opt/homebrew/opt/llvm@18/bin/llvm-config
export LLVM_CONFIG
.DEFAULT_GOAL := all

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
include plugin.mk

ELF := activity-probe.elf
LITE_ELF := activity-probe-lite.elf
CFLAGS := -Wall -Wextra -Werror -O2 -DPROBE_VERSION=\"0.4.10-beta.1\" \
	-DTRACKER_VERSION=\"1.41\" -DDASHBOARD_VERSION=\"1.41.0\"
LITE_CFLAGS := -Wall -Wextra -Werror -O2 \
	-DPROBE_VERSION=\"compat-0.1\" -DTRACKER_VERSION=\"compat-0.1\" \
	-DDASHBOARD_VERSION=\"compat-0.1\" -DLITE_INSTALLER

.PHONY: all clean

all: $(ELF)

$(ELF): main.c tracker.c tracker.h http_server.c http_server.h \
		offline_update.c offline_update.h
	$(CC) $(CFLAGS) -o $@ main.c tracker.c http_server.c offline_update.c

$(LITE_ELF): lite_main.c tracker.c tracker.h http_server.c http_server.h \
		offline_update.c offline_update.h
	$(CC) $(LITE_CFLAGS) -o $@ lite_main.c tracker.c http_server.c \
		offline_update.c

lite: $(LITE_ELF)

clean:
	rm -f $(ELF) $(LITE_ELF) $(PLUGIN)
