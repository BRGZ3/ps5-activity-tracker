# Media PKG build

[Русская версия](README.ru.md)

This directory builds the production Media PKG with Title ID `ACTV00002`. The
tile opens `http://127.0.0.1:12888/`; it does not launch the ELF directly.

## Inputs

- `../dashboard/index.html` — dashboard;
- `../dashboard/library.html` — installed-game library page;
- `../activity-probe/activity-probe.elf` — runtime (requires the static
  `ps5-payload-sqlite` SDK package);
- `app/sce_sys/param.json` — Media metadata;
- `playlog-logo.png` — source icon copied into the PS5 shortcut;
- `build-carrier.js` — PNG + dashboard/library/ELF carrier;
- `builder/` — LibProsperoPKG wrapper.

`make` first builds the ELF from source, then creates the carrier and runs the
PKG validator. Results are written to `dist/`, which is ignored by Git.
The carrier stores the main dashboard, `library.html`, and the runtime in that
order; the updater treats the library entry as optional for compatibility with
older two-entry carriers.

## Requirements

```text
PS5 Payload SDK
prospero-clang
Node.js
Docker for linux/arm64
LibProsperoPKG checkout or a prebuilt `LibProsperoPkg.dll`
```

The PS5 SDK must also contain `ps5-payload-sqlite` under its
`target/user/homebrew` sysroot. Build that package from the PS5 Payload
pacbrew repository before invoking `make`, or set `SQLITE_PREFIX` to the
package's sysroot prefix.

The outer `release-build/Makefile` forwards `SQLITE_PREFIX` to the runtime
build and copies `playlog-logo.png` into the shortcut before adding the
offline carrier.

Set the builder path when needed:

```bash
LIBPROSPEROPKG_DIR=/path/to/LibProsperoPKG-build make
```

If the directory does not contain the source checkout, the builder uses
`$LIBPROSPEROPKG_DIR/LibProsperoPkg.dll`.

For a reproducible publication, pin the LibProsperoPKG revision and Docker
image digest. They remain overrideable through Make variables so the build
can run on the maintainer's machine.

Do not commit `dist/`, `builder/bin/`, `builder/obj/`, `icon0.png` or ELF files.
Upload the resulting PKG and ELF as GitHub Release Assets.
