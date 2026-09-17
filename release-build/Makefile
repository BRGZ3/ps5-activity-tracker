LIBPROSPEROPKG_DIR ?= /private/tmp/LibProsperoPKG-build
IMAGE ?= mcr.microsoft.com/dotnet/sdk:10.0
OUTPUT := dist
ICON_SOURCE ?= playlog-logo.png
PS5_PAYLOAD_SDK ?= /tmp/ps5-payload-sdk-install
LLVM_CONFIG ?= /opt/homebrew/opt/llvm@18/bin/llvm-config
SQLITE_PREFIX ?= $(PS5_PAYLOAD_SDK)/target/user/homebrew
PACKAGE_VERSION ?= 1.70
TRACKER_VERSION ?= 1.64
DASHBOARD_VERSION ?= 1.49.0

.PHONY: all clean

all: runtime package

.PHONY: runtime package

runtime:
	$(MAKE) -C ../activity-probe PS5_PAYLOAD_SDK="$(PS5_PAYLOAD_SDK)" \
		LLVM_CONFIG="$(LLVM_CONFIG)" SQLITE_PREFIX="$(SQLITE_PREFIX)"

package:
	mkdir -p app/sce_sys $(OUTPUT)
	cp "$(ICON_SOURCE)" app/sce_sys/base-icon.png
	node build-carrier.js app/sce_sys/base-icon.png app/sce_sys/icon0.png \
		../dashboard/index.html ../dashboard/library.html \
		../activity-probe/activity-probe.elf \
		$(PACKAGE_VERSION) $(TRACKER_VERSION) $(DASHBOARD_VERSION)
	docker run --rm --platform linux/arm64 \
		-v "$(CURDIR):/work" \
		-v "$(LIBPROSPEROPKG_DIR):/libprosperopkg" \
		-w /work $(IMAGE) \
		dotnet run --project builder/PlaylogReleaseBuilder.csproj \
		-p:LibProsperoPkgDir=/libprosperopkg -- app $(OUTPUT)
	cp ../activity-probe/activity-probe.elf $(OUTPUT)/Playlog.elf

clean:
	rm -rf builder/bin builder/obj app/sce_sys/base-icon.png \
		app/sce_sys/icon0.png $(OUTPUT)
