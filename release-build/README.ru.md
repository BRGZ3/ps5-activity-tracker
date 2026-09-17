# Сборка Media PKG

[English version](README.md)

Эта папка собирает production Media PKG с Title ID `ACTV00002`.
Плитка открывает `http://127.0.0.1:12888/`; она не запускает ELF напрямую.

## Входные файлы

- `../dashboard/index.html` — dashboard;
- `../dashboard/library.html` — страница библиотеки установленных игр;
- `../activity-probe/activity-probe.elf` — runtime (требуется статический
  пакет `ps5-payload-sqlite` в SDK);
- `app/sce_sys/param.json` — Media metadata;
- `playlog-logo.png` — исходная иконка, которая попадает в ярлык PS5;
- `build-carrier.js` — PNG + dashboard/library/ELF carrier;
- `builder/` — LibProsperoPKG wrapper.

`make` сначала собирает ELF из исходников, затем создаёт carrier и запускает
валидатор PKG. Результаты находятся в `dist/`, который исключён из Git.
В carrier по порядку лежат основной dashboard, `library.html` и runtime;
updater считает запись библиотеки необязательной для совместимости со старыми
carrier из двух записей.

## Требования

```text
PS5 Payload SDK
prospero-clang
Node.js
Docker с linux/arm64
исходники LibProsperoPKG или готовый `LibProsperoPkg.dll`
```

В sysroot SDK также должен быть `ps5-payload-sqlite` в
`target/user/homebrew`. Перед `make` соберите этот пакет из репозитория PS5
Payload pacbrew либо задайте `SQLITE_PREFIX` для другого расположения.

Внешний `release-build/Makefile` передаёт `SQLITE_PREFIX` во внутреннюю
сборку runtime и копирует `playlog-logo.png` в ярлык перед добавлением offline
carrier.

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
