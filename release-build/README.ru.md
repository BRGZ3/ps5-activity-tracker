# Сборка Media PKG

[English version](README.md)

Эта папка собирает production Media PKG с Title ID `ACTV00002`.
Плитка открывает `http://127.0.0.1:12888/`; она не запускает ELF напрямую.

## Входные файлы

- `../dashboard/index.html` — dashboard;
- `../activity-probe/activity-probe.elf` — runtime;
- `app/sce_sys/param.json` — Media metadata;
- `build-carrier.js` — PNG + dashboard/ELF carrier;
- `builder/` — LibProsperoPKG wrapper.

`make` сначала собирает ELF из исходников, затем создаёт carrier и запускает
валидатор PKG. Результаты находятся в `dist/`, который исключён из Git.

## Требования

```text
PS5 Payload SDK
prospero-clang
Node.js
Docker с linux/arm64
исходники LibProsperoPKG или готовый `LibProsperoPkg.dll`
```

Путь к builder можно задать:

```bash
LIBPROSPEROPKG_DIR=/path/to/LibProsperoPKG-build make
```

Если в каталоге нет исходников, builder использует
`$LIBPROSPEROPKG_DIR/LibProsperoPkg.dll`.

Для воспроизводимой публикации нужно дополнительно зафиксировать revision
LibProsperoPKG и digest Docker image. Их можно переопределить через переменные
Makefile, чтобы сборка выполнялась на машине сопровождающего.

Не коммитьте `dist/`, `builder/bin/`, `builder/obj/`, `icon0.png` и ELF.
Загрузите полученные PKG и ELF как GitHub Release Assets.
