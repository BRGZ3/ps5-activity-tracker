# Media PKG v1.0 build

[Русская версия](README.ru.md)

This directory builds the production Media PKG with Title ID `ACTV00002`. The
tile opens `http://127.0.0.1:12888/`; it does not launch the ELF directly.

## Inputs

- `../dashboard/index.html` — dashboard;
- `../activity-probe/activity-probe.elf` — runtime;
- `app/sce_sys/param.json` — Media metadata;
- `build-carrier.js` — PNG + dashboard/ELF carrier;
- `builder/` — LibProsperoPKG wrapper.

`make` first builds the ELF from source, then creates the carrier and runs the
PKG validator. Results are written to `dist/`, which is ignored by Git.

## Requirements

```text
PS5 Payload SDK
prospero-clang
Node.js
Docker for linux/arm64
LibProsperoPKG checkout
```

Set the builder path when needed:

```bash
LIBPROSPEROPKG_DIR=/path/to/LibProsperoPKG-build make
```

For a reproducible publication, pin the LibProsperoPKG revision and Docker
image digest. v1.0 keeps them overrideable through Make variables so the build
can run on the maintainer's machine.

Do not commit `dist/`, `builder/bin/`, `builder/obj/`, `icon0.png` or ELF files.
Upload them as GitHub Release Assets using [GITHUB_UPLOAD.md](../GITHUB_UPLOAD.md).
